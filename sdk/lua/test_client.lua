local source = debug.getinfo(1, "S").source
if source:sub(1, 1) == "@" then
    local dir = source:sub(2):match("(.*[/\\])")
    if dir then package.path = dir .. "?.lua;" .. package.path end
end
local sdk = require("synapse_gateway")

local seen = {}
local script = {
    { status = 402, body = '{"price_usdc":0.001,"payment_wallet":"0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5","network":"base","transaction_reference":"txref-1","expires_at_unix":1700000000}', headers = {} },
    { status = 200, body = "{}", headers = { ["X-Transaction-Hash"] = "0xabc" } },
}
local client = sdk.client({
    base_url = "http://127.0.0.1:8080",
    wallet = "0x2222222222222222222222222222222222222222",
    verifying_contract = "0x9344ebe7bea6c6f0357a285fed7a79d98af32ff5",
    signer = function() return "0x" .. string.rep("ab", 65) end,
    transport = {
        exchange = function(method, url, body, headers)
            seen[#seen + 1] = { method = method, url = url, body = body, headers = headers }
            return table.remove(script, 1)
        end,
    },
})

local paid = client.invoke("GET", "/api/v1/chat", "", function()
    return { tx_hash = "0xabc" }
end)
assert(paid.status == 200, "status")
assert(paid.headers["X-Transaction-Hash"] == "0xabc", "hash")
local found = false
for _, pair in ipairs(seen[2].headers) do
    if pair[1] == "X-Payment-Reference" and pair[2] == "txref-1" then found = true end
end
assert(found, "reference")
print("lua sdk ok")
