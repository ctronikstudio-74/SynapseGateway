export interface SignerInput {
  method: string;
  path: string;
  body: string;
  wallet: string;
  quoteReference: string;
  expiryUnix: number;
  chainId: number;
  verifyingContract: string;
}

export type Signer = (input: SignerInput) => Promise<string> | string;

export interface Quote {
  priceUsdc: number;
  balanceUsdc: number;
  paymentWallet: string;
  network: string;
  transactionReference: string;
  expiresAtUnix: number;
  topUp: string;
}

export interface Payment {
  txHash?: string;
  authorizationJson?: string;
}

export interface CallResult {
  status: number;
  body: string;
  headers: Record<string, string>;
  quote?: Quote;
}

export const chainIds: Record<string, number> = {
  ethereum_mainnet: 1,
  ethereum: 1,
  base: 8453,
  arbitrum: 42161,
  polygon: 137,
  bnb_chain: 56,
  bnb: 56,
};

export interface Transport {
  exchange(method: string, url: string, body: string, headers: Array<[string, string]>): Promise<CallResult>;
}

export class FetchTransport implements Transport {
  async exchange(method: string, url: string, body: string, headers: Array<[string, string]>): Promise<CallResult> {
    const response = await fetch(url, { method, body: body || undefined, headers: headers });
    const text = await response.text();
    const collected: Record<string, string> = {};
    response.headers.forEach((value, key) => {
      collected[key] = value;
    });
    return { status: response.status, body: text, headers: collected };
  }
}

export class Client {
  readonly wallet: string;
  readonly verifyingContract: string;
  readonly chainId: number;
  private readonly baseUrl: string;
  private readonly signer: Signer;
  private readonly transport: Transport;

  constructor(baseUrl: string, wallet: string, signer: Signer, verifyingContract: string, chainId = 8453, transport: Transport = new FetchTransport()) {
    if (!baseUrl || !wallet || !verifyingContract || chainId <= 0) {
      throw new Error("base URL, wallet, collection address, and chain id are required");
    }
    this.baseUrl = baseUrl;
    this.wallet = wallet;
    this.signer = signer;
    this.verifyingContract = verifyingContract;
    this.chainId = chainId;
    this.transport = transport;
  }

  health(): Promise<CallResult> {
    return this.exchange("GET", "/health", "", [], false);
  }

  catalog(): Promise<CallResult> {
    return this.exchange("GET", "/mcp", "", [], false);
  }

  mcp(method: string, params: unknown = {}, id = "1"): Promise<CallResult> {
    const payload: Record<string, unknown> = { jsonrpc: "2.0", method, params };
    if (method !== "notifications/initialized") {
      payload.id = id;
    }
    return this.exchange("POST", "/mcp", JSON.stringify(payload), [], false);
  }

  call(method: string, path: string, body = "", options: Partial<{
    txHash: string;
    authorizationJson: string;
    proof: string;
    reference: string;
    expiryUnix: number;
    chainId: number;
  }> = {}): Promise<CallResult> {
    return this.exchange(method, path, body, [], true, options);
  }

  async invoke(method: string, path: string, body: string, payer: (quote: Quote) => Promise<Payment> | Payment): Promise<CallResult> {
    const first = await this.call(method, path, body);
    if (first.status !== 402 || !first.quote?.transactionReference) {
      return first;
    }
    const payment = await payer(first.quote);
    if (!payment.txHash && !payment.authorizationJson) {
      return first;
    }
    return this.call(method, path, body, {
      txHash: payment.txHash,
      authorizationJson: payment.authorizationJson,
      reference: first.quote.transactionReference,
      expiryUnix: first.quote.expiresAtUnix,
      chainId: chainIds[first.quote.network] ?? this.chainId,
    });
  }

  deposit(txHash: string): Promise<CallResult> {
    return this.call("POST", "/v1/credits", JSON.stringify({ tx_hash: txHash }));
  }

  refund(txHash: string, walletAddress: string, gasFeeUsdc: number): Promise<CallResult> {
    return this.call("POST", "/v1/refunds", JSON.stringify({
      tx_hash: txHash,
      wallet_address: walletAddress,
      gas_fee_usdc: gasFeeUsdc,
    }));
  }

  async decide(method: string, path: string, body = "", proxy = "nginx"): Promise<CallResult> {
    const signature = await this.sign(method, path, body, "", 0, this.chainId);
    const headers: Array<[string, string]> = [
      ["X-Agent-Signature", signature],
      ["X-Agent-Wallet", this.wallet],
    ];
    const kind = proxy.toLowerCase();
    if (kind === "traefik") {
      headers.push(["X-Forwarded-Method", method], ["X-Forwarded-Uri", path]);
    } else if (kind === "envoy") {
      headers.push(["X-Original-Method", method], ["X-Envoy-Original-Path", path]);
    } else {
      headers.push(["X-Original-Method", method], ["X-Original-URI", path]);
    }
    return this.exchange("POST", "/v1/auth", body, headers, false);
  }

  private async sign(method: string, path: string, body: string, reference: string, expiry: number, chainId: number): Promise<string> {
    return this.signer({
      method,
      path,
      body,
      wallet: this.wallet,
      quoteReference: reference,
      expiryUnix: reference ? expiry : 0,
      chainId,
      verifyingContract: this.verifyingContract,
    });
  }

  private async exchange(
    method: string,
    path: string,
    body: string,
    headers: Array<[string, string]>,
    signed: boolean,
    options: Partial<{ txHash: string; authorizationJson: string; proof: string; reference: string; expiryUnix: number; chainId: number }> = {},
  ): Promise<CallResult> {
    const outbound = [...headers];
    if (signed) {
      const signature = await this.sign(method, path, body, options.reference ?? "", options.expiryUnix ?? 0, options.chainId || this.chainId);
      outbound.push(["X-Agent-Signature", signature], ["X-Agent-Wallet", this.wallet]);
      if (options.txHash) outbound.push(["X-Payment-Tx", options.txHash]);
      if (options.reference) outbound.push(["X-Payment-Reference", options.reference]);
      if (options.authorizationJson) outbound.push(["X-Payment-Authorization", options.authorizationJson]);
      if (options.proof) outbound.push(["X-Agent-Proof", options.proof]);
    }
    if (body) outbound.push(["Content-Type", "application/json"]);
    const url = path.startsWith("http://") || path.startsWith("https://")
      ? path
      : this.baseUrl.replace(/\/$/, "") + (path.startsWith("/") ? path : `/${path}`);
    const result = await this.transport.exchange(method, url, body, outbound);
    if (result.status === 402) {
      result.quote = readQuote(result.body, this.verifyingContract);
    }
    return result;
  }
}

function readQuote(body: string, verifyingContract: string): Quote | undefined {
  let parsed: Record<string, unknown>;
  try {
    parsed = JSON.parse(body) as Record<string, unknown>;
  } catch {
    return undefined;
  }
  const quote: Quote = {
    priceUsdc: Number(parsed.price_usdc ?? 0),
    balanceUsdc: Number(parsed.balance_usdc ?? 0),
    paymentWallet: String(parsed.payment_wallet ?? ""),
    network: String(parsed.network ?? ""),
    transactionReference: String(parsed.transaction_reference ?? ""),
    expiresAtUnix: Number(parsed.expires_at_unix ?? 0),
    topUp: String(parsed.top_up ?? ""),
  };
  if (quote.paymentWallet && quote.paymentWallet.toLowerCase() !== verifyingContract.toLowerCase()) {
    throw new Error("quote collection address does not match the client verifying contract");
  }
  return quote;
}

export function header(result: CallResult, name: string): string {
  const wanted = name.toLowerCase();
  for (const [key, value] of Object.entries(result.headers)) {
    if (key.toLowerCase() === wanted) return value;
  }
  return "";
}
