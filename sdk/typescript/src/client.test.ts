import assert from "node:assert/strict";
import { Client, header, type CallResult, type Payment, type Quote } from "./client.ts";

class ScriptTransport {
  script: CallResult[] = [];
  seen: Array<{ method: string; url: string; body: string; headers: Array<[string, string]> }> = [];

  async exchange(method: string, url: string, body: string, headers: Array<[string, string]>): Promise<CallResult> {
    this.seen.push({ method, url, body, headers });
    const next = this.script.shift();
    if (!next) throw new Error("missing scripted response");
    return next;
  }
}

const quoteBody = JSON.stringify({
  error: "payment required",
  price_usdc: 0.001,
  payment_wallet: "0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5",
  network: "base",
  transaction_reference: "txref-1",
  expires_at_unix: 1700000000,
});

const transport = new ScriptTransport();
const signed: string[] = [];
const client = new Client(
  "http://127.0.0.1:8080",
  "0x2222222222222222222222222222222222222222",
  (input) => {
    signed.push(input.quoteReference);
    return "0x" + "ab".repeat(65);
  },
  "0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5",
  8453,
  transport,
);

transport.script.push({ status: 402, body: quoteBody, headers: {} });
transport.script.push({ status: 200, body: "{}", headers: { "X-Transaction-Hash": "0xabc" } });
const paid = await client.invoke("GET", "/api/v1/chat", "", (_quote: Quote): Payment => ({ txHash: "0xabc" }));
assert.equal(paid.status, 200);
assert.equal(header(paid, "X-Transaction-Hash"), "0xabc");
assert.deepEqual(signed, ["", "txref-1"]);
const retry = Object.fromEntries(transport.seen[1].headers);
assert.equal(retry["X-Payment-Reference"], "txref-1");
assert.equal(retry["X-Payment-Tx"], "0xabc");

transport.script.push({ status: 200, body: "{}", headers: {} });
await client.decide("GET", "/api/free", "", "traefik");
const decision = Object.fromEntries(transport.seen[2].headers);
assert.equal(decision["X-Forwarded-Uri"], "/api/free");
assert.equal(transport.seen[2].url, "http://127.0.0.1:8080/v1/auth");
console.log("typescript sdk ok");
