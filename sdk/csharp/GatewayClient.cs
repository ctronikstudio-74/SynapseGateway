using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;

namespace Ctronik.Synapse.Gateway;

public interface ISigner
{
    string Sign(string method, string path, string body, string wallet, string quoteReference,
        ulong expiryUnix, ulong chainId, string verifyingContract);
}

public sealed record Quote(double PriceUsdc, double BalanceUsdc, string PaymentWallet, string Network,
    string TransactionReference, ulong ExpiresAtUnix, string TopUp);

public sealed record Payment(string TxHash, string AuthorizationJson);

public sealed class CallResult
{
    public int Status { get; init; }
    public string Body { get; init; } = "";
    public Dictionary<string, string> Headers { get; init; } = new(StringComparer.OrdinalIgnoreCase);
    public Quote? Quote { get; set; }

    public string Header(string name) => Headers.TryGetValue(name, out var value) ? value : "";
}

public interface ITransport
{
    Task<CallResult> ExchangeAsync(string method, string url, string body, IReadOnlyList<(string Name, string Value)> headers);
}

public sealed class GatewayClient
{
    static readonly Dictionary<string, ulong> ChainIds = new(StringComparer.OrdinalIgnoreCase)
    {
        ["ethereum_mainnet"] = 1, ["ethereum"] = 1, ["base"] = 8453, ["arbitrum"] = 42161,
        ["polygon"] = 137, ["bnb_chain"] = 56, ["bnb"] = 56,
    };

    readonly string baseUrl;
    readonly ISigner signer;
    readonly ulong chainId;
    readonly ITransport transport;

    public GatewayClient(string baseUrl, string wallet, ISigner signer, string verifyingContract, ulong chainId = 8453, ITransport? transport = null)
    {
        if (string.IsNullOrWhiteSpace(baseUrl) || string.IsNullOrWhiteSpace(wallet)
            || string.IsNullOrWhiteSpace(verifyingContract) || chainId == 0)
            throw new ArgumentException("base URL, wallet, collection address, and chain id are required");
        this.baseUrl = baseUrl.TrimEnd('/');
        Wallet = wallet;
        this.signer = signer;
        VerifyingContract = verifyingContract;
        this.chainId = chainId;
        this.transport = transport ?? new HttpTransport();
    }

    public string Wallet { get; }
    public string VerifyingContract { get; }

    public Task<CallResult> HealthAsync() => ExchangeAsync("GET", "/health", "", [], false, default);
    public Task<CallResult> CatalogAsync() => ExchangeAsync("GET", "/mcp", "", [], false, default);

    public Task<CallResult> McpAsync(string method, object? parameters = null, string id = "1")
    {
        var payload = new Dictionary<string, object?> { ["jsonrpc"] = "2.0", ["method"] = method, ["params"] = parameters ?? new { } };
        if (method != "notifications/initialized") payload["id"] = id;
        return ExchangeAsync("POST", "/mcp", JsonSerializer.Serialize(payload), [], false, default);
    }

    public Task<CallResult> CallAsync(string method, string path, string body = "", CallOptions? options = null)
        => ExchangeAsync(method, path, body, [], true, options ?? new CallOptions());

    public async Task<CallResult> InvokeAsync(string method, string path, string body, Func<Quote, Payment> payer)
    {
        var first = await CallAsync(method, path, body);
        if (first.Status != 402 || first.Quote == null || string.IsNullOrEmpty(first.Quote.TransactionReference)) return first;
        var payment = payer(first.Quote);
        if (string.IsNullOrEmpty(payment.TxHash) && string.IsNullOrEmpty(payment.AuthorizationJson)) return first;
        var chain = ChainIds.TryGetValue(first.Quote.Network, out var found) ? found : chainId;
        return await CallAsync(method, path, body, new CallOptions(payment.TxHash, payment.AuthorizationJson, "", first.Quote.TransactionReference, first.Quote.ExpiresAtUnix, chain));
    }

    public Task<CallResult> DepositAsync(string txHash)
        => CallAsync("POST", "/v1/credits", JsonSerializer.Serialize(new { tx_hash = txHash }));

    public Task<CallResult> RefundAsync(string txHash, string walletAddress, double gasFeeUsdc)
        => CallAsync("POST", "/v1/refunds", JsonSerializer.Serialize(new { tx_hash = txHash, wallet_address = walletAddress, gas_fee_usdc = gasFeeUsdc }));

    public async Task<CallResult> DecideAsync(string method, string path, string body = "", string proxy = "nginx")
    {
        var signature = signer.Sign(method, path, body, Wallet, "", 0, chainId, VerifyingContract);
        var headers = new List<(string, string)> { ("X-Agent-Signature", signature), ("X-Agent-Wallet", Wallet) };
        switch (proxy.ToLowerInvariant())
        {
            case "traefik":
                headers.Add(("X-Forwarded-Method", method));
                headers.Add(("X-Forwarded-Uri", path));
                break;
            case "envoy":
                headers.Add(("X-Original-Method", method));
                headers.Add(("X-Envoy-Original-Path", path));
                break;
            default:
                headers.Add(("X-Original-Method", method));
                headers.Add(("X-Original-URI", path));
                break;
        }
        return await ExchangeAsync("POST", "/v1/auth", body, headers, false, default);
    }

    async Task<CallResult> ExchangeAsync(string method, string path, string body, List<(string Name, string Value)> headers, bool signed, CallOptions? options)
    {
        options ??= new CallOptions();
        var outbound = new List<(string, string)>(headers);
        if (signed)
        {
            var chain = options.ChainId == 0 ? chainId : options.ChainId;
            var expiry = string.IsNullOrEmpty(options.Reference) ? 0 : options.ExpiryUnix;
            var signature = signer.Sign(method, path, body, Wallet, options.Reference, expiry, chain, VerifyingContract);
            outbound.Add(("X-Agent-Signature", signature));
            outbound.Add(("X-Agent-Wallet", Wallet));
            if (!string.IsNullOrEmpty(options.TxHash)) outbound.Add(("X-Payment-Tx", options.TxHash));
            if (!string.IsNullOrEmpty(options.Reference)) outbound.Add(("X-Payment-Reference", options.Reference));
            if (!string.IsNullOrEmpty(options.AuthorizationJson)) outbound.Add(("X-Payment-Authorization", options.AuthorizationJson));
            if (!string.IsNullOrEmpty(options.Proof)) outbound.Add(("X-Agent-Proof", options.Proof));
        }
        if (body.Length > 0) outbound.Add(("Content-Type", "application/json"));
        var url = path.StartsWith("http://", StringComparison.Ordinal) || path.StartsWith("https://", StringComparison.Ordinal)
            ? path
            : baseUrl + (path.StartsWith('/') ? path : "/" + path);
        var result = await transport.ExchangeAsync(method, url, body, outbound);
        if (result.Status == 402) result.Quote = ReadQuote(result.Body, VerifyingContract);
        return result;
    }

    static Quote ReadQuote(string body, string verifyingContract)
    {
        using var document = JsonDocument.Parse(body);
        var root = document.RootElement;
        var wallet = Text(root, "payment_wallet");
        if (wallet.Length > 0 && !wallet.Equals(verifyingContract, StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("quote collection address does not match the client verifying contract");
        return new Quote(
            root.TryGetProperty("price_usdc", out var price) ? price.GetDouble() : 0,
            root.TryGetProperty("balance_usdc", out var balance) ? balance.GetDouble() : 0,
            wallet,
            Text(root, "network"),
            Text(root, "transaction_reference"),
            root.TryGetProperty("expires_at_unix", out var expiry) ? expiry.GetUInt64() : 0,
            Text(root, "top_up"));
    }

    static string Text(JsonElement root, string name)
        => root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() ?? "" : "";

    sealed class HttpTransport : ITransport
    {
        readonly HttpClient client = new() { Timeout = TimeSpan.FromSeconds(30) };

        public async Task<CallResult> ExchangeAsync(string method, string url, string body, IReadOnlyList<(string Name, string Value)> headers)
        {
            using var request = new HttpRequestMessage(new HttpMethod(method), url);
            foreach (var (name, value) in headers) request.Headers.TryAddWithoutValidation(name, value);
            if (body.Length > 0)
            {
                request.Content = new StringContent(body, Encoding.UTF8);
                request.Content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
            }
            using var response = await client.SendAsync(request);
            var text = await response.Content.ReadAsStringAsync();
            var collected = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var header in response.Headers) collected[header.Key] = string.Join(",", header.Value);
            foreach (var header in response.Content.Headers) collected[header.Key] = string.Join(",", header.Value);
            return new CallResult { Status = (int)response.StatusCode, Body = text, Headers = collected };
        }
    }
}

public sealed record CallOptions(
    string TxHash = "",
    string AuthorizationJson = "",
    string Proof = "",
    string Reference = "",
    ulong ExpiryUnix = 0,
    ulong ChainId = 0);
