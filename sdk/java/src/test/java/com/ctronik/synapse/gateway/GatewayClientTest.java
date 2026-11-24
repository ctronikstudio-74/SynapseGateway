package com.ctronik.synapse.gateway;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public final class GatewayClientTest {
    public static void main(String[] args) throws Exception {
        List<String> urls = new ArrayList<>();
        List<List<Map.Entry<String, String>>> seen = new ArrayList<>();
        List<GatewayClient.CallResult> script = new ArrayList<>();
        script.add(new GatewayClient.CallResult(402,
                "{\"price_usdc\":0.001,\"payment_wallet\":\"0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5\",\"network\":\"base\",\"transaction_reference\":\"txref-1\",\"expires_at_unix\":1700000000}",
                Map.of()));
        script.add(new GatewayClient.CallResult(200, "{}", Map.of("X-Transaction-Hash", "0xabc")));
        GatewayClient.Transport transport = (method, url, body, headers) -> {
            urls.add(url);
            seen.add(headers);
            return script.remove(0);
        };
        GatewayClient client = new GatewayClient(
                "http://127.0.0.1:8080",
                "0x2222222222222222222222222222222222222222",
                (method, path, body, wallet, reference, expiry, chainId, contract) -> "0x" + "ab".repeat(65),
                "0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5",
                8453,
                transport);
        GatewayClient.CallResult paid = client.invoke("GET", "/api/v1/chat", "", quote -> new GatewayClient.Payment("0xabc", ""));
        if (paid.status != 200) throw new AssertionError(paid.status);
        if (!"0xabc".equals(paid.header("X-Transaction-Hash"))) throw new AssertionError("hash");
        if (!urls.get(1).endsWith("/api/v1/chat")) throw new AssertionError(urls.get(1));
        boolean reference = false;
        for (Map.Entry<String, String> header : seen.get(1)) {
            if ("X-Payment-Reference".equals(header.getKey()) && "txref-1".equals(header.getValue())) reference = true;
        }
        if (!reference) throw new AssertionError("reference");
        System.out.println("java sdk ok");
    }
}
