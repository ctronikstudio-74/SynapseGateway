# Load checks

The automated check is `performance_test`. It drives the verification pipeline in-process and prints throughput and p95 latency. Release builds are expected to reach at least 1000 requests per second. Debug builds assert a lower floor.

Against a running gateway, with a zero-price route so the blockchain is not on the path:

```bash
wrk -t4 -c64 -d30s \
  -H "X-Agent-Signature: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
  -H "X-Agent-Wallet: 0x2222222222222222222222222222222222222222" \
  http://127.0.0.1:8080/api/free
```

```bash
ab -n 10000 -c 64 \
  -H "X-Agent-Signature: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" \
  -H "X-Agent-Wallet: 0x2222222222222222222222222222222222222222" \
  http://127.0.0.1:8080/api/free
```

Cache hit rate and blockchain latency are on the Prometheus port. Identity and reputation entries live for 300 seconds. Verified payments live for 24 hours. The backend pool keeps at least 10 idle HTTP connections per origin.
