# SynapseGateway SDK 0.3.0

Client per chiamare un SynapseGateway già in esecuzione. Il core e questi SDK restano GPL-3.0-only. Non aprono una porta, non incassano e non sostituiscono il processo `SynapseGateway`.

La firma è EIP-712, dominio `AgentGateway` versione `1`. Il contratto di verifica è il wallet CREATE2 stampato da `SynapseGateway check`. La prima chiamata firma un riferimento vuoto e scadenza 0. Dopo un HTTP 402 il retry firma `transaction_reference` e `expires_at_unix` del preventivo, con il chain id della rete indicata (`base` è 8453).

Ogni linguaggio espone le stesse operazioni:

| Operazione | HTTP |
| --- | --- |
| `health` | `GET /health` |
| `catalog` | `GET /mcp` |
| `mcp` | `POST /mcp` con `initialize`, `tools/list`, `notifications/initialized` |
| `call` / `invoke` | route firmata. `invoke` rilancia il 402 con hash o autorizzazione EIP-3009 |
| `deposit` | `POST /v1/credits` |
| `refund` | `POST /v1/refunds` |
| `decide` | `POST /v1/auth` per nginx, Envoy o Traefik |

Il pagamento on-chain resta dell'agente: l'SDK consegna l'hash o il JSON `X-Payment-Authorization`, non muove i fondi da solo.

## Linguaggi

| Cartella | Uso |
| --- | --- |
| `cpp` | Libreria `synapse_gateway_sdk`, firma con la stessa funzione del gateway |
| `python` | Pacchetto `synapse_gateway`. La firma di test è iniettata. `EthAccountSigner` usa `eth-account` |
| `typescript` | `sdk/typescript/src/client.ts`, firma iniettata, trasporto `fetch` |
| `java` | `com.ctronik.synapse.gateway.GatewayClient` |
| `csharp` | `Ctronik.Synapse.Gateway.GatewayClient` |
| `lua` | `require("synapse_gateway")`. Senza un transport proprio usa LuaSocket |

Esempio Python:

```python
from synapse_gateway import Client, Payment

client = Client("http://127.0.0.1:8080", wallet, signer, collection, chain_id=8453)
result = client.invoke("GET", "/api/v1/chat", "", lambda quote: Payment(tx_hash=tx_hash))
```

`collection` è l'indirizzo di `payment_wallet=` stampato da `SynapseGateway check`.

## Edizioni a pagamento

Il sorgente 0.3.0 e questo SDK non diventano proprietari. Le edizioni enterprise si ordinano sul portale C-Tronik, pagina prodotti: <https://c-tronik.com/products/>.

| Prodotto | Licenza del sorgente | Voce a pagamento |
| --- | --- | --- |
| SynapseGateway | GPL-3.0-only | Supporto di produzione e distribuzione enterprise. Listino da €4.800 a €24.000 l'anno, IVA esclusa |
| SynapseMFT | protocollo LDP pubblicato a parte | Governance Suite da €6.000 l'anno, Virtual Desktop €1.200 a posto, Event Bus da €4.800 |
| Synapse Firewall | core MIT | Console commerciale da €480 a posto. Il core WFP resta gratuito |

I prezzi, la licenza perpetua dove esiste e il passo di pagamento sono sulla pagina del prodotto. Un ordine sul portale arriva fino all'importo del bonifico: la pagina non addebita una carta.
