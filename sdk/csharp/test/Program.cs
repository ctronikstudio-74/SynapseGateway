using Ctronik.Synapse.Gateway;

var seen = new List<(string Method, string Url, string Body, IReadOnlyList<(string Name, string Value)> Headers)>();
var script = new Queue<CallResult>();
script.Enqueue(new CallResult
{
    Status = 402,
    Body = """{"price_usdc":0.001,"payment_wallet":"0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5","network":"base","transaction_reference":"txref-1","expires_at_unix":1700000000}""",
});
script.Enqueue(new CallResult { Status = 200, Body = "{}", Headers = new Dictionary<string, string> { ["X-Transaction-Hash"] = "0xabc" } });

ITransport transport = new Script(seen, script);
var client = new GatewayClient(
    "http://127.0.0.1:8080",
    "0x2222222222222222222222222222222222222222",
    new FixedSigner(),
    "0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5",
    8453,
    transport);
var paid = await client.InvokeAsync("GET", "/api/v1/chat", "", _ => new Payment("0xabc", ""));
if (paid.Status != 200 || paid.Header("X-Transaction-Hash") != "0xabc") throw new Exception("payment retry failed");
if (seen[1].Headers.All(header => header.Name != "X-Payment-Reference" || header.Value != "txref-1"))
    throw new Exception("quote reference was not sent");
Console.WriteLine("csharp sdk ok");

sealed class FixedSigner : ISigner
{
    public string Sign(string method, string path, string body, string wallet, string quoteReference, ulong expiryUnix, ulong chainId, string verifyingContract)
        => "0x" + new string('a', 130);
}

sealed class Script(List<(string Method, string Url, string Body, IReadOnlyList<(string Name, string Value)> Headers)> seen, Queue<CallResult> script) : ITransport
{
    public Task<CallResult> ExchangeAsync(string method, string url, string body, IReadOnlyList<(string Name, string Value)> headers)
    {
        seen.Add((method, url, body, headers));
        return Task.FromResult(script.Dequeue());
    }
}
