package com.ctronik.synapse.gateway;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.function.Function;

/**
 * Java client for SynapseGateway 0.3.0.
 * The signer returns the 65-byte EIP-712 signature of the AgentGateway domain, version 1.
 */
public final class GatewayClient {
    public interface Signer {
        String sign(String method, String path, String body, String wallet, String quoteReference,
                    long expiryUnix, long chainId, String verifyingContract);
    }

    public static final class Quote {
        public double priceUsdc;
        public double balanceUsdc;
        public String paymentWallet = "";
        public String network = "";
        public String transactionReference = "";
        public long expiresAtUnix;
        public String topUp = "";
    }

    public static final class Payment {
        public final String txHash;
        public final String authorizationJson;

        public Payment(String txHash, String authorizationJson) {
            this.txHash = txHash == null ? "" : txHash;
            this.authorizationJson = authorizationJson == null ? "" : authorizationJson;
        }
    }

    public static final class CallResult {
        public final int status;
        public final String body;
        public final Map<String, String> headers;
        public Quote quote;

        CallResult(int status, String body, Map<String, String> headers) {
            this.status = status;
            this.body = body;
            this.headers = headers;
        }

        public String header(String name) {
            for (Map.Entry<String, String> entry : headers.entrySet()) {
                if (entry.getKey().equalsIgnoreCase(name)) {
                    return entry.getValue();
                }
            }
            return "";
        }
    }

    public interface Transport {
        CallResult exchange(String method, String url, String body, List<Map.Entry<String, String>> headers) throws Exception;
    }

    private final String baseUrl;
    private final String wallet;
    private final Signer signer;
    private final String verifyingContract;
    private final long chainId;
    private final Transport transport;

    public GatewayClient(String baseUrl, String wallet, Signer signer, String verifyingContract, long chainId, Transport transport) {
        if (baseUrl == null || baseUrl.isBlank() || wallet == null || wallet.isBlank()
                || verifyingContract == null || verifyingContract.isBlank() || chainId <= 0 || signer == null) {
            throw new IllegalArgumentException("base URL, wallet, collection address, and chain id are required");
        }
        this.baseUrl = baseUrl.endsWith("/") ? baseUrl.substring(0, baseUrl.length() - 1) : baseUrl;
        this.wallet = wallet;
        this.signer = signer;
        this.verifyingContract = verifyingContract;
        this.chainId = chainId;
        this.transport = transport == null ? new JdkTransport() : transport;
    }

    public String wallet() {
        return wallet;
    }

    public CallResult health() throws Exception {
        return exchange("GET", "/health", "", List.of(), false, "", "", "", "", 0, chainId);
    }

    public CallResult catalog() throws Exception {
        return exchange("GET", "/mcp", "", List.of(), false, "", "", "", "", 0, chainId);
    }

    public CallResult mcp(String method, String paramsJson, String id) throws Exception {
        String params = paramsJson == null || paramsJson.isBlank() ? "{}" : paramsJson;
        StringBuilder payload = new StringBuilder();
        payload.append("{\"jsonrpc\":\"2.0\",\"method\":").append(quote(method)).append(",\"params\":").append(params);
        if (!"notifications/initialized".equals(method)) {
            payload.append(",\"id\":").append(quote(id));
        }
        payload.append('}');
        return exchange("POST", "/mcp", payload.toString(), List.of(), false, "", "", "", "", 0, chainId);
    }

    public CallResult call(String method, String path, String body, String txHash, String authorizationJson,
                           String proof, String reference, long expiryUnix, long chain) throws Exception {
        return exchange(method, path, body == null ? "" : body, List.of(), true,
                empty(txHash), empty(authorizationJson), empty(proof), empty(reference), expiryUnix,
                chain == 0 ? chainId : chain);
    }

    public CallResult invoke(String method, String path, String body, Function<Quote, Payment> payer) throws Exception {
        CallResult first = call(method, path, body, "", "", "", "", 0, chainId);
        if (first.status != 402 || first.quote == null || first.quote.transactionReference.isEmpty() || payer == null) {
            return first;
        }
        Payment payment = payer.apply(first.quote);
        if (payment == null || (payment.txHash.isEmpty() && payment.authorizationJson.isEmpty())) {
            return first;
        }
        long chain = chainId(first.quote.network);
        return call(method, path, body, payment.txHash, payment.authorizationJson, "",
                first.quote.transactionReference, first.quote.expiresAtUnix, chain == 0 ? chainId : chain);
    }

    public CallResult deposit(String txHash) throws Exception {
        return call("POST", "/v1/credits", "{\"tx_hash\":" + quote(txHash) + "}", "", "", "", "", 0, chainId);
    }

    public CallResult refund(String txHash, String walletAddress, double gasFeeUsdc) throws Exception {
        String payload = "{\"tx_hash\":" + quote(txHash) + ",\"wallet_address\":" + quote(walletAddress)
                + ",\"gas_fee_usdc\":" + gasFeeUsdc + "}";
        return call("POST", "/v1/refunds", payload, "", "", "", "", 0, chainId);
    }

    public CallResult decide(String method, String path, String body, String proxy) throws Exception {
        String signature = signer.sign(method, path, body == null ? "" : body, wallet, "", 0, chainId, verifyingContract);
        List<Map.Entry<String, String>> headers = new ArrayList<>();
        headers.add(Map.entry("X-Agent-Signature", signature));
        headers.add(Map.entry("X-Agent-Wallet", wallet));
        String kind = proxy == null ? "nginx" : proxy.toLowerCase(Locale.ROOT);
        if ("traefik".equals(kind)) {
            headers.add(Map.entry("X-Forwarded-Method", method));
            headers.add(Map.entry("X-Forwarded-Uri", path));
        } else if ("envoy".equals(kind)) {
            headers.add(Map.entry("X-Original-Method", method));
            headers.add(Map.entry("X-Envoy-Original-Path", path));
        } else {
            headers.add(Map.entry("X-Original-Method", method));
            headers.add(Map.entry("X-Original-URI", path));
        }
        return exchange("POST", "/v1/auth", body == null ? "" : body, headers, false, "", "", "", "", 0, chainId);
    }

    private CallResult exchange(String method, String path, String body, List<Map.Entry<String, String>> headers,
                                boolean signed, String txHash, String authorizationJson, String proof,
                                String reference, long expiryUnix, long chain) throws Exception {
        List<Map.Entry<String, String>> outbound = new ArrayList<>(headers);
        if (signed) {
            long expiry = reference.isEmpty() ? 0 : expiryUnix;
            String signature = signer.sign(method, path, body, wallet, reference, expiry, chain, verifyingContract);
            outbound.add(Map.entry("X-Agent-Signature", signature));
            outbound.add(Map.entry("X-Agent-Wallet", wallet));
            if (!txHash.isEmpty()) outbound.add(Map.entry("X-Payment-Tx", txHash));
            if (!reference.isEmpty()) outbound.add(Map.entry("X-Payment-Reference", reference));
            if (!authorizationJson.isEmpty()) outbound.add(Map.entry("X-Payment-Authorization", authorizationJson));
            if (!proof.isEmpty()) outbound.add(Map.entry("X-Agent-Proof", proof));
        }
        if (!body.isEmpty()) outbound.add(Map.entry("Content-Type", "application/json"));
        String url = path.startsWith("http://") || path.startsWith("https://") ? path : baseUrl + (path.startsWith("/") ? path : "/" + path);
        CallResult result = transport.exchange(method, url, body, outbound);
        if (result.status == 402) {
            result.quote = readQuote(result.body, verifyingContract);
        }
        return result;
    }

    static long chainId(String network) {
        if (network == null) return 0;
        return switch (network.toLowerCase(Locale.ROOT)) {
            case "ethereum_mainnet", "ethereum" -> 1L;
            case "base" -> 8453L;
            case "arbitrum" -> 42161L;
            case "polygon" -> 137L;
            case "bnb_chain", "bnb" -> 56L;
            default -> 0L;
        };
    }

    private static Quote readQuote(String body, String verifyingContract) {
        Quote quote = new Quote();
        quote.priceUsdc = number(body, "price_usdc");
        quote.balanceUsdc = number(body, "balance_usdc");
        quote.paymentWallet = text(body, "payment_wallet");
        quote.network = text(body, "network");
        quote.transactionReference = text(body, "transaction_reference");
        quote.expiresAtUnix = (long) number(body, "expires_at_unix");
        quote.topUp = text(body, "top_up");
        if (!quote.paymentWallet.isEmpty() && !quote.paymentWallet.equalsIgnoreCase(verifyingContract)) {
            throw new IllegalArgumentException("quote collection address does not match the client verifying contract");
        }
        return quote;
    }

    private static String text(String json, String key) {
        int found = json.indexOf("\"" + key + "\"");
        if (found < 0) return "";
        int colon = json.indexOf(':', found);
        int start = json.indexOf('"', colon + 1);
        int end = json.indexOf('"', start + 1);
        if (start < 0 || end < 0) return "";
        return json.substring(start + 1, end);
    }

    private static double number(String json, String key) {
        int found = json.indexOf("\"" + key + "\"");
        if (found < 0) return 0;
        int colon = json.indexOf(':', found);
        int index = colon + 1;
        while (index < json.length() && Character.isWhitespace(json.charAt(index))) index++;
        int end = index;
        while (end < json.length() && "0123456789.-".indexOf(json.charAt(end)) >= 0) end++;
        if (end == index) return 0;
        return Double.parseDouble(json.substring(index, end));
    }

    private static String quote(String value) {
        return "\"" + value.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    private static String empty(String value) {
        return value == null ? "" : value;
    }

    static final class JdkTransport implements Transport {
        private final HttpClient client = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();

        @Override
        public CallResult exchange(String method, String url, String body, List<Map.Entry<String, String>> headers) throws Exception {
            HttpRequest.Builder builder = HttpRequest.newBuilder(URI.create(url)).timeout(Duration.ofSeconds(30));
            for (Map.Entry<String, String> header : headers) {
                builder.header(header.getKey(), header.getValue());
            }
            HttpRequest.BodyPublisher publisher = body.isEmpty()
                    ? HttpRequest.BodyPublishers.noBody()
                    : HttpRequest.BodyPublishers.ofString(body);
            builder.method(method, publisher);
            HttpResponse<String> response = client.send(builder.build(), HttpResponse.BodyHandlers.ofString());
            Map<String, String> collected = new LinkedHashMap<>();
            response.headers().map().forEach((name, values) -> {
                if (!values.isEmpty()) collected.put(name, values.get(0));
            });
            return new CallResult(response.statusCode(), response.body(), collected);
        }
    }
}
