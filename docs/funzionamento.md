# Come funziona AgentGateway

AgentGateway è un reverse proxy C++20 per API usate da agenti. Intercetta la richiesta HTTP, applica il protocollo x402 e i controlli ERC-8004, poi inoltra la chiamata al backend configurato. Il binario è `gateway_node`.

## Avvio

```text
gateway_node config/gateway.dev.toml
```

Senza argomenti il processo cerca `config/gateway.dev.toml`. Il file viene letto, i segnaposto `${NOME}` sono sostituiti con le variabili d’ambiente e la configurazione è validata. Se il file manca o un campo è invalido, il processo scrive l’errore su stderr ed esce con codice 1.

All’avvio il nodo:

1. Apre i client JSON-RPC delle reti abilitate e calcola il wallet di incasso con CREATE2 a partire da `payment_wallet_seed`. Lo stesso seme produce lo stesso indirizzo su Ethereum, Base, Arbitrum, Polygon e BNB Chain.
2. Carica `data/payment-state.json` se esiste: rimborsi, verifiche in corso e hash già visti.
3. Espone Prometheus su `monitoring_port` quando `prometheus_enabled` è vero.
4. Accetta HTTP su `listen_address` e `listen_port`.
5. Ogni 10 secondi ripete il controllo di salute delle reti. Ogni 60 secondi aggiorna il gas. Una rete che non risponde viene esclusa dalla scelta del pagamento entro 30 secondi.
6. Osserva la data di modifica del TOML. Una nuova configurazione valida sostituisce quella attiva anche per le richieste già entrate.

Il file di sviluppo lascia gli endpoint a prezzo zero e punta il backend a `http://localhost:3000`. Il template di produzione è `config/gateway.prod.toml`: gli indirizzi e gli URL arrivano dall’ambiente.

## Percorso di una richiesta

`GET /health` non richiede firma. Risponde 200 se tutte le reti abilitate sono sane, altrimenti 503 con il dettaglio per rete.

Ogni altro path segue questa sequenza.

1. **Firma.** L’header `X-Agent-Signature` deve esserci, avere tra 64 e 512 caratteri e contenere solo esadecimali o base64. Se manca, la risposta è 401. Il controllo è sulla forma dell’header: il gateway non ricostruisce il firmatario.
2. **Wallet.** L’indirizzo è letto da `X-Agent-Wallet`, `X-Wallet-Address` o `X-Payment-Wallet`. Deve essere `0x` seguito da 40 cifre esadecimali.
3. **Percorso.** Il path è confrontato con le espressioni regolari di `[[endpoints]]`. Se nessuna combacia, vale `[default_endpoint]`.
4. **Limite.** Per ogni wallet si conta una finestra mobile di 60 secondi. Oltre `rate_limit_per_minute` la risposta è 429 con `Retry-After`.
5. **Pagamento.** Se `price_usdc` è 0, la verifica è immediata e non interroga la chain. Se il prezzo è positivo e manca `X-Payment-Tx`, la risposta è 402 con prezzo USDC, wallet di incasso, rete col gas più basso tra quelle sane e un riferimento `txref-…`. L’agente paga in USDC (6 decimali) almeno il 99,9% del prezzo quotato, poi ripete la chiamata con l’hash. Per 24 ore lo stesso hash è servito dalla cache. Dopo la scadenza è rifiutato con 409.
6. **Identità.** Il registry ERC-721 della rete scelta deve assegnare un NFT al wallet. Il risultato resta in cache 300 secondi. Un fallimento è 403.
7. **Reputazione.** Il punteggio è la media pesata di tasso di successo (0,5), tempo di risposta (0,2) e feedback (0,3), ristretta a `[0, 1]`. Sotto `trust_threshold` la risposta è 403. Senza storico, `new_agents_allowed` decide se far passare l’agente con punteggio 0. Le prove zkML, TEE o staking richieste dall’endpoint devono chiudersi entro 200 ms.
8. **Backend.** Gli header di pagamento, la firma e i caratteri di controllo vengono tolti. Il gateway aggiunge wallet, token, tipo di identità, punteggio, rete e numero di richieste. La connessione ha 5 secondi di tempo; la lettura della risposta ne ha 30. Il pool HTTP tiene almeno 10 connessioni per origine.
9. **Risposta.** Lo stato del backend torna all’agente con `X-Payment-Amount` e `X-Transaction-Hash`. Un 5xx del backend, un 502 di connessione o un 504 di timeout segnano il pagamento come rimborsabile.

## Rimborso

`POST /v1/refunds` con firma e corpo JSON:

```json
{"tx_hash":"0x…","wallet_address":"0x…","gas_fee_usdc":0.0001}
```

Il rimborso è accettato solo se il gas in USDC è strettamente minore dell’importo pagato. L’importo registrato è la differenza. Il gateway scrive il registro su disco e non invia una transazione di rimborso sulla chain: il trasferimento resta un’operazione esterna, da eseguire con l’importo salvato.

## Arresto

SIGTERM, Ctrl+C o la chiusura della console smettono di accettare connessioni. Le richieste già dentro il processo hanno fino a 30 secondi. Poi lo stato viene riscritto, i client RPC vengono chiusi e il processo esce con codice 0. Una richiesta arrivata durante lo scarico riceve 503.

## Dove guardare

| Bisogno | File |
| --- | --- |
| Header, codici, curl | [api.md](api.md) |
| Build, Docker, systemd, Prometheus | [deployment.md](deployment.md) |
| Esempio commentato | `config/gateway.example.toml` |
| Contratti di esempio | `tests/contracts/README.md` |

I log su stdout sono JSON, con `event` uguale a `request` o `security`. Le metriche Prometheus includono conteggi, importi, latenze, errori backend, cache e salute delle reti.
