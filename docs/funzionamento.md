# Come funziona SynapseGateway

Versione 0.3.0. I client in `sdk/` (C++, Python, TypeScript, Java, C#, Lua) firmano e chiamano questo processo. Il core resta GPL-3.0-only. Supporto e distribuzione enterprise, insieme a SynapseMFT e Synapse Firewall, si ordinano su <https://c-tronik.com/products/>. Il catalogo completo, in inglese, è [docs/features.md](features.md). `SynapseGateway check config/gateway.dev.toml` valida il file e stampa il wallet di incasso senza aprire la porta. All’avvio lo stesso indirizzo finisce nel log. `GET /v1/auth` è la decisione per Envoy, Traefik e nginx. `GET /mcp` elenca le route. Uno span OTLP parte solo se `opentelemetry_enabled` è vero e `otlp_endpoint` è valorizzato. SynapseGateway è un reverse proxy C++20 per API usate da agenti. Intercetta la richiesta HTTP, applica il protocollo x402 e i controlli ERC-8004, poi inoltra la chiamata al backend configurato. Il binario è `SynapseGateway`. Il dominio EIP-712 firmato dagli agenti resta `AgentGateway` versione `1`.

## Avvio

```text
SynapseGateway config/gateway.dev.toml
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

1. **Firma.** L’header `X-Agent-Signature` è una firma EIP-712 sul metodo, sul path, sull’hash del body, sul wallet, sul riferimento e sulla scadenza. Il dominio è `AgentGateway` versione `1`. Se il recupero non coincide col wallet, il gateway chiede `isValidSignature` (ERC-1271). Se manca o non coincide, la risposta è 401.
2. **Wallet.** L’indirizzo è letto da `X-Agent-Wallet`, `X-Wallet-Address` o `X-Payment-Wallet`. Deve essere `0x` seguito da 40 cifre esadecimali.
3. **Percorso.** Il path è confrontato con le espressioni regolari di `[[endpoints]]`. Se nessuna combacia, vale `[default_endpoint]`.
4. **Limite.** Per ogni wallet si conta una finestra mobile di 60 secondi. Oltre `rate_limit_per_minute` la risposta è 429 con `Retry-After`.
5. **Pagamento.** Se `price_usdc` è 0, la verifica è immediata e non interroga la chain. Se il prezzo è positivo e manca `X-Payment-Tx`, la risposta è 402 con prezzo USDC, wallet di incasso, rete col gas più basso tra quelle sane e un riferimento `txref-…`. L’agente deve trasferire lo USDC di `usdc_contract`, oppure uno stablecoin elencato in `stablecoins`, dal proprio wallet verso il wallet di incasso, per almeno il 99,9% del prezzo quotato. I decimali sono quelli del contratto trasferito. Un altro token, un valore nativo o un mittente diverso non saldano la richiesta. Se nella stessa transazione ci sono USDC e un altro stablecoin, conta lo USDC. Il dominio della firma ha il chain id della rete del preventivo e il wallet di incasso come contratto verificatore. Il 402 include `transaction_reference` e `expires_at_unix`. Il preventivo vale 15 minuti e lega percorso, prezzo, wallet e rete. L’agente ripete la chiamata con l’hash e con `X-Payment-Reference`. Per 24 ore lo stesso hash è servito dalla cache solo per quel preventivo. Un altro percorso o un altro prezzo nello stesso intervallo è rifiutato. Dopo la scadenza l’hash è rifiutato con 409.
6. **Identità.** Il registry ERC-721 della rete scelta deve assegnare un NFT al wallet. Il risultato resta in cache 300 secondi. Se `minimum_nft_age_seconds` è positivo, la registrazione deve essere almeno così vecchia. Un fallimento è 403. Prima del pagamento, un wallet in `denylist` o fuori da `allowlist` è già rifiutato con 403.
7. **Reputazione.** Il punteggio è la media pesata di tasso di successo (0,5), tempo di risposta (0,2) e feedback (0,3), ristretta a `[0, 1]`. Sotto `trust_threshold` la risposta è 403. Senza storico, `new_agents_allowed` decide se far passare l’agente con punteggio 0. Le prove zkML, TEE o staking richieste dall’endpoint devono chiudersi entro 200 ms.
8. **Backend.** Gli header di pagamento, la firma e i caratteri di controllo vengono tolti. Il gateway aggiunge wallet, token, tipo di identità, punteggio, rete e numero di richieste. La connessione ha 5 secondi di tempo; la lettura della risposta ne ha 30. Il pool HTTP tiene almeno 10 connessioni per origine.
9. **Risposta.** Lo stato del backend torna all’agente con `X-Payment-Amount` e `X-Transaction-Hash`. Un 5xx del backend, un 502 di connessione o un 504 di timeout segnano il pagamento come rimborsabile.

## Policy dei wallet

`[wallet_policy]` si ricarica insieme al TOML. `denylist` rifiuta il wallet con 403 prima del pagamento, del rimborso e del credito. `minimum_nft_age_seconds` a zero non applica un’età minima; un valore positivo richiede che il timestamp di registrazione dell’NFT sia almeno così vecchio. `allowlist` su un endpoint, se non è vuota, accetta solo gli indirizzi elencati. Una lista vuota non aggiunge vincoli.

## Stablecoin

`usdc_contract` resta il token predefinito. `stablecoins` aggiunge altri contratti, per esempio USDT o EURC, con i decimali di quel contratto. L’importo numerico del preventivo vale per il token trasferito. Se nella stessa transazione ci sono USDC e un altro stablecoin, conta lo USDC. Incasso EIP-3009 e rimborso on-chain usano solo lo USDC.

## Rimborso

`POST /v1/refunds` con firma e corpo JSON:

```json
{"tx_hash":"0x…","wallet_address":"0x…","gas_fee_usdc":0.0001}
```

Il rimborso è accettato solo se il gas in USDC è strettamente minore dell’importo pagato. L’importo registrato è la differenza. Se `blockchain.treasury_private_key` è configurata, il gateway firma e invia un `transfer` ERC-20 di quel residuo e chiude il registro solo dopo la ricevuta finalizzata. Senza quella chiave il registro si chiude in locale e il trasferimento resta esterno.

In alternativa all’hash, il retry può portare `X-Payment-Authorization`: una `transferWithAuthorization` EIP-3009 firmata dall’agente. Il gateway la verifica, la invia con la chiave di tesoreria e accetta la chiamata solo a ricevuta finalizzata. Lo stesso nonce non viene ritrasmesso.

Se un endpoint ha `gas_budget_usdc` strettamente maggiore di `price_usdc`, la chiamata scala il saldo prepagato. Un saldo insufficiente risponde 402 con l’invito `POST /v1/credits`. Il deposito è un transfer USDC finalizzato dall’agente al wallet di incasso; lo stesso hash non può pagare anche una chiamata. Un fallimento di identità, reputazione o backend riaccredita il prezzo.

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
