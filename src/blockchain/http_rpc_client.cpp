#include "gateway/blockchain/rpc_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <openssl/ssl.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace gateway::blockchain {
namespace {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string port;
    std::string target;
};

ParsedUrl parseUrl(const std::string& url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos || scheme_end == 0) {
        throw std::runtime_error("RPC URL is missing a scheme: " + url);
    }
    ParsedUrl parsed;
    parsed.scheme = url.substr(0, scheme_end);
    std::string rest = url.substr(scheme_end + 3);
    const auto slash = rest.find('/');
    const std::string host_port = slash == std::string::npos ? rest : rest.substr(0, slash);
    parsed.target = slash == std::string::npos ? "/" : rest.substr(slash);
    if (host_port.empty()) {
        throw std::runtime_error("RPC URL is missing a host: " + url);
    }
    const auto colon = host_port.rfind(':');
    if (colon != std::string::npos && host_port.find(':') == colon) {
        parsed.host = host_port.substr(0, colon);
        parsed.port = host_port.substr(colon + 1);
    } else {
        parsed.host = host_port;
    }
    if (parsed.host.empty()) {
        throw std::runtime_error("RPC URL is missing a host: " + url);
    }
    if (parsed.port.empty()) {
        if (parsed.scheme == "https" || parsed.scheme == "wss") {
            parsed.port = "443";
        } else if (parsed.scheme == "http" || parsed.scheme == "ws") {
            parsed.port = "80";
        } else {
            throw std::runtime_error("unsupported RPC scheme: " + parsed.scheme);
        }
    }
    return parsed;
}

class PoolGuard {
public:
    PoolGuard(std::mutex& mutex, std::condition_variable& cv, std::size_t& in_use, std::size_t limit)
        : mutex_(mutex), cv_(cv), in_use_(in_use) {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return in_use_ < limit; });
        ++in_use_;
    }

    ~PoolGuard() {
        {
            std::lock_guard lock(mutex_);
            --in_use_;
        }
        cv_.notify_one();
    }

    PoolGuard(const PoolGuard&) = delete;
    PoolGuard& operator=(const PoolGuard&) = delete;

private:
    std::mutex& mutex_;
    std::condition_variable& cv_;
    std::size_t& in_use_;
};

template <typename Stream>
std::string postJson(Stream& stream, const ParsedUrl& url, const std::string& body) {
    http::request<http::string_body> request{http::verb::post, url.target, 11};
    request.set(http::field::host, url.host);
    request.set(http::field::content_type, "application/json");
    request.set(http::field::user_agent, "SynapseGateway");
    request.set(http::field::connection, "keep-alive");
    request.body() = body;
    request.prepare_payload();

    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    if (response.result_int() >= 400) {
        throw std::runtime_error("RPC HTTP status " + std::to_string(response.result_int()));
    }
    return response.body();
}

std::string exchangeHttp(net::io_context& ioc, const ParsedUrl& url, const std::string& body) {
    tcp::resolver resolver(ioc);
    const auto endpoints = resolver.resolve(url.host, url.port);
    beast::tcp_stream stream(ioc);
    stream.expires_after(std::chrono::seconds(5));
    stream.connect(endpoints);
    const std::string response = postJson(stream, url, body);
    beast::error_code error;
    stream.socket().shutdown(tcp::socket::shutdown_both, error);
    return response;
}

std::string exchangeHttps(net::io_context& ioc,
                          ssl::context& ssl_context,
                          const ParsedUrl& url,
                          const std::string& body) {
    tcp::resolver resolver(ioc);
    const auto endpoints = resolver.resolve(url.host, url.port);
    beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_context);
    if (!SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str())) {
        throw std::runtime_error("failed to set TLS server name");
    }
    beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
    beast::get_lowest_layer(stream).connect(endpoints);
    stream.handshake(ssl::stream_base::client);
    const std::string response = postJson(stream, url, body);
    beast::error_code error;
    stream.shutdown(error);
    return response;
}

std::string exchangeWebSocket(net::io_context& ioc, const ParsedUrl& url, const std::string& body, bool tls,
                              ssl::context& ssl_context) {
    tcp::resolver resolver(ioc);
    const auto endpoints = resolver.resolve(url.host, url.port);
    auto read_frame = [&](auto& ws) {
        ws.set_option(beast::websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws.handshake(url.host, url.target);
        ws.write(net::buffer(body));
        beast::flat_buffer buffer;
        ws.read(buffer);
        return beast::buffers_to_string(buffer.data());
    };

    if (!tls) {
        beast::websocket::stream<beast::tcp_stream> ws(ioc);
        beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));
        beast::get_lowest_layer(ws).connect(endpoints);
        beast::get_lowest_layer(ws).expires_never();
        return read_frame(ws);
    }

    beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws(ioc, ssl_context);
    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), url.host.c_str())) {
        throw std::runtime_error("failed to set TLS server name");
    }
    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(5));
    beast::get_lowest_layer(ws).connect(endpoints);
    ws.next_layer().handshake(ssl::stream_base::client);
    beast::get_lowest_layer(ws).expires_never();
    return read_frame(ws);
}

nlohmann::json parseResult(const std::string& body) {
    const nlohmann::json parsed = nlohmann::json::parse(body);
    if (parsed.contains("error") && !parsed["error"].is_null()) {
        throw std::runtime_error("RPC error: " + parsed["error"].dump());
    }
    if (!parsed.contains("result")) {
        throw std::runtime_error("RPC response has no result");
    }
    return parsed["result"];
}

}  // namespace

HttpRpcClient::HttpRpcClient(std::size_t pool_size) : pool_size_(pool_size == 0 ? 1 : pool_size) {}

nlohmann::json HttpRpcClient::call(const std::string& rpc_url,
                                   const std::string& method,
                                   const nlohmann::json& params) {
    static std::mutex pool_mutex;
    static std::condition_variable pool_cv;
    static std::size_t in_use = 0;
    static ssl::context ssl_context{ssl::context::tls_client};

    static const bool kSslReady = [] {
        ssl_context.set_default_verify_paths();
        ssl_context.set_verify_mode(ssl::verify_peer);
        SSL_CTX_set_session_cache_mode(ssl_context.native_handle(), SSL_SESS_CACHE_CLIENT);
        return true;
    }();
    (void)kSslReady;

    const ParsedUrl url = parseUrl(rpc_url);
    nlohmann::json request = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}};
    const std::string payload = request.dump();

    PoolGuard guard(pool_mutex, pool_cv, in_use, pool_size_);
    std::exception_ptr failure;
    for (int attempt = 0; attempt < 2; ++attempt) {
        try {
            net::io_context ioc;
            std::string body;
            if (url.scheme == "https") {
                body = exchangeHttps(ioc, ssl_context, url, payload);
            } else if (url.scheme == "http") {
                body = exchangeHttp(ioc, url, payload);
            } else if (url.scheme == "ws") {
                body = exchangeWebSocket(ioc, url, payload, false, ssl_context);
            } else if (url.scheme == "wss") {
                body = exchangeWebSocket(ioc, url, payload, true, ssl_context);
            } else {
                throw std::runtime_error("unsupported RPC scheme: " + url.scheme);
            }
            return parseResult(body);
        } catch (const std::exception&) {
            failure = std::current_exception();
        }
    }
    std::rethrow_exception(failure);
}

}  // namespace gateway::blockchain
