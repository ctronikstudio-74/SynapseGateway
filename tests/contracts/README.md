# Registry stubs

Continuous integration talks to an in-process JSON-RPC server (`tests/integration/gateway_flow_test.cpp` and `tests/support/local_http.h`). That mock answers `eth_blockNumber`, `eth_gasPrice`, and the other calls the connector makes, so a Hardhat or Ganache install is not required to build or test the gateway.

The Solidity files in `mocks/` are the shapes the ABI decoder expects: ERC-721 `balanceOf` / `tokenOfOwnerByIndex` / `getIdentityData`, reputation `getReputation`, and validation methods for zkML, TEE, and stake. Deploy them with your own toolchain when you want a chain instead of the mock. A minimal Hardhat layout is in `hardhat/` and is optional:

```bash
cd tests/contracts/hardhat
npm install
npx hardhat test
```
