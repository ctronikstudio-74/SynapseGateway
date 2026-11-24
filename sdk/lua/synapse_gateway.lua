local M = {}

M.chain_ids = {
    ethereum_mainnet = 1,
    ethereum = 1,
    base = 8453,
    arbitrum = 42161,
    polygon = 137,
    bnb_chain = 56,
    bnb = 56,
}

local function quote_json(value)
    return '"' .. tostring(value):gsub('\\', '\\\\'):gsub('"', '\\"') .. '"'
end

local function header_map(list)
    local map = {}
    for _, pair in ipairs(list) do
        map[pair[1]] = pair[2]
    end
    return map
end

local function field(body, key)
    return body:match('"' .. key .. '"%s*:%s*"([^"]*)"') or ""
end

local function number_field(body, key)
    return tonumber(body:match('"' .. key .. '"%s*:%s*([%d%.%-]+)')) or 0
end

local function read_quote(body, verifying_contract)
    local quote = {
        price_usdc = number_field(body, "price_usdc"),
        balance_usdc = number_field(body, "balance_usdc"),
        payment_wallet = field(body, "payment_wallet"),
        network = field(body, "network"),
        transaction_reference = field(body, "transaction_reference"),
        expires_at_unix = number_field(body, "expires_at_unix"),
        top_up = field(body, "top_up"),
    }
    if quote.payment_wallet ~= "" and quote.payment_wallet:lower() ~= verifying_contract:lower() then
        error("quote collection address does not match the client verifying contract")
    end
    return quote
end

function M.client(options)
    if not options.base_url or not options.wallet or not options.signer or not options.verifying_contract then
        error("base URL, wallet, signer, and collection address are required")
    end
    local base = options.base_url:gsub("/+$", "")
    local chain_id = options.chain_id or 8453
    local transport = options.transport
    if not transport then
        local http = require("socket.http")
        local ltn12 = require("ltn12")
        transport = {
            exchange = function(method, url, body, headers)
                local response = {}
                local _, status, response_headers = http.request({
                    url = url,
                    method = method,
                    headers = header_map(headers),
                    source = body ~= "" and ltn12.source.string(body) or nil,
                    sink = ltn12.sink.table(response),
                })
                return { status = status, body = table.concat(response), headers = response_headers or {} }
            end,
        }
    end

    local function sign(method, path, body, reference, expiry, chain)
        return options.signer({
            method = method,
            path = path,
            body = body,
            wallet = options.wallet,
            quote_reference = reference,
            expiry_unix = reference ~= "" and expiry or 0,
            chain_id = chain,
            verifying_contract = options.verifying_contract,
        })
    end

    local function exchange(method, path, body, headers, signed, call)
        call = call or {}
        local outbound = {}
        for _, pair in ipairs(headers or {}) do
            outbound[#outbound + 1] = pair
        end
        if signed then
            local chain = call.chain_id or chain_id
            local reference = call.reference or ""
            outbound[#outbound + 1] = { "X-Agent-Signature", sign(method, path, body, reference, call.expiry_unix or 0, chain) }
            outbound[#outbound + 1] = { "X-Agent-Wallet", options.wallet }
            if call.tx_hash and call.tx_hash ~= "" then outbound[#outbound + 1] = { "X-Payment-Tx", call.tx_hash } end
            if reference ~= "" then outbound[#outbound + 1] = { "X-Payment-Reference", reference } end
            if call.authorization_json and call.authorization_json ~= "" then
                outbound[#outbound + 1] = { "X-Payment-Authorization", call.authorization_json }
            end
            if call.proof and call.proof ~= "" then outbound[#outbound + 1] = { "X-Agent-Proof", call.proof } end
        end
        if body ~= "" then outbound[#outbound + 1] = { "Content-Type", "application/json" } end
        local url = path
        if path:sub(1, 7) ~= "http://" and path:sub(1, 8) ~= "https://" then
            url = base .. (path:sub(1, 1) == "/" and path or "/" .. path)
        end
        local result = transport.exchange(method, url, body, outbound)
        if result.status == 402 then
            result.quote = read_quote(result.body, options.verifying_contract)
        end
        return result
    end

    local api = { wallet = options.wallet }

    function api.health()
        return exchange("GET", "/health", "", {}, false)
    end

    function api.catalog()
        return exchange("GET", "/mcp", "", {}, false)
    end

    function api.mcp(method, params_json, id)
        local payload = '{"jsonrpc":"2.0","method":' .. quote_json(method) .. ',"params":' .. (params_json or "{}")
        if method ~= "notifications/initialized" then
            payload = payload .. ',"id":' .. quote_json(id or "1")
        end
        return exchange("POST", "/mcp", payload .. "}", {}, false)
    end

    function api.call(method, path, body, call)
        return exchange(method, path, body or "", {}, true, call)
    end

    function api.invoke(method, path, body, payer)
        local first = api.call(method, path, body, {})
        if first.status ~= 402 or not first.quote or first.quote.transaction_reference == "" then
            return first
        end
        local payment = payer(first.quote)
        if not payment or ((not payment.tx_hash or payment.tx_hash == "") and (not payment.authorization_json or payment.authorization_json == "")) then
            return first
        end
        return api.call(method, path, body, {
            tx_hash = payment.tx_hash or "",
            authorization_json = payment.authorization_json or "",
            reference = first.quote.transaction_reference,
            expiry_unix = first.quote.expires_at_unix,
            chain_id = M.chain_ids[first.quote.network] or chain_id,
        })
    end

    function api.deposit(tx_hash)
        return api.call("POST", "/v1/credits", '{"tx_hash":' .. quote_json(tx_hash) .. "}")
    end

    function api.refund(tx_hash, wallet_address, gas_fee_usdc)
        local payload = '{"tx_hash":' .. quote_json(tx_hash) .. ',"wallet_address":' .. quote_json(wallet_address)
            .. ',"gas_fee_usdc":' .. tostring(gas_fee_usdc) .. "}"
        return api.call("POST", "/v1/refunds", payload)
    end

    function api.decide(method, path, body, proxy)
        body = body or ""
        local signature = sign(method, path, body, "", 0, chain_id)
        local headers = { { "X-Agent-Signature", signature }, { "X-Agent-Wallet", options.wallet } }
        proxy = (proxy or "nginx"):lower()
        if proxy == "traefik" then
            headers[#headers + 1] = { "X-Forwarded-Method", method }
            headers[#headers + 1] = { "X-Forwarded-Uri", path }
        elseif proxy == "envoy" then
            headers[#headers + 1] = { "X-Original-Method", method }
            headers[#headers + 1] = { "X-Envoy-Original-Path", path }
        else
            headers[#headers + 1] = { "X-Original-Method", method }
            headers[#headers + 1] = { "X-Original-URI", path }
        end
        return exchange("POST", "/v1/auth", body, headers, false)
    end

    return api
end

return M
