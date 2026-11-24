import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from synapse_gateway import Client, Payment


class ScriptTransport:
    def __init__(self) -> None:
        self.script = []
        self.seen = []

    def exchange(self, method, url, body, headers):
        self.seen.append((method, url, body, headers))
        status, payload, response_headers = self.script.pop(0)
        from synapse_gateway.client import CallResult

        return CallResult(status, payload, response_headers)


class ClientTest(unittest.TestCase):
    def client(self):
        transport = ScriptTransport()
        signer_calls = []

        def sign(**fields):
            signer_calls.append(fields)
            return "0x" + "ab" * 65

        client = Client(
            "http://127.0.0.1:8080",
            "0x2222222222222222222222222222222222222222",
            sign,
            "0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5",
            transport=transport,
        )
        return client, transport, signer_calls

    def test_health_is_unsigned(self):
        client, transport, calls = self.client()
        transport.script.append((200, "{}", {}))
        self.assertEqual(client.health().status, 200)
        self.assertEqual(transport.seen[0][1], "http://127.0.0.1:8080/health")
        self.assertEqual(calls, [])

    def test_paid_retry_uses_the_quote(self):
        client, transport, calls = self.client()
        quote = {
            "error": "payment required",
            "price_usdc": 0.001,
            "payment_wallet": "0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5",
            "network": "base",
            "transaction_reference": "txref-1",
            "expires_at_unix": 1700000000,
        }
        transport.script.append((402, json.dumps(quote), {}))
        transport.script.append((200, "{}", {"X-Transaction-Hash": "0xabc"}))
        result = client.invoke("GET", "/api/v1/chat", "", lambda item: Payment(tx_hash="0xabc"))
        self.assertEqual(result.status, 200)
        self.assertEqual(result.header("X-Transaction-Hash"), "0xabc")
        self.assertEqual(calls[0]["quote_reference"], "")
        self.assertEqual(calls[0]["expiry_unix"], 0)
        self.assertEqual(calls[1]["quote_reference"], "txref-1")
        self.assertEqual(calls[1]["expiry_unix"], 1700000000)
        self.assertEqual(calls[1]["chain_id"], 8453)
        second = dict(transport.seen[1][3])
        self.assertEqual(second["X-Payment-Tx"], "0xabc")
        self.assertEqual(second["X-Payment-Reference"], "txref-1")

    def test_credits_refunds_mcp_and_decision(self):
        client, transport, _calls = self.client()
        transport.script.extend([(200, "{}", {}), (200, "{}", {}), (200, "{}", {}), (200, '{"allowed":true}', {})])
        self.assertEqual(client.deposit("0xdeposit").status, 200)
        self.assertIn("0xdeposit", transport.seen[0][2])
        self.assertTrue(transport.seen[0][1].endswith("/v1/credits"))
        self.assertEqual(client.refund("0xpaid", client.wallet, 0.0001).status, 200)
        self.assertIn("gas_fee_usdc", transport.seen[1][2])
        self.assertEqual(client.mcp("tools/list").status, 200)
        self.assertIn("tools/list", transport.seen[2][2])
        decision = client.decide("POST", "/api/v1/chat", '{"n":1}', "envoy")
        self.assertEqual(decision.status, 200)
        headers = dict(transport.seen[3][3])
        self.assertEqual(headers["X-Envoy-Original-Path"], "/api/v1/chat")
        self.assertEqual(headers["X-Original-Method"], "POST")
        self.assertEqual(transport.seen[3][2], '{"n":1}')


if __name__ == "__main__":
    unittest.main()
