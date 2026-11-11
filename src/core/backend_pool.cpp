#include "gateway/core/backend_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gateway::core {
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

ParsedUrl parseBackendUrl(const std::string& url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos || scheme_end == 0) {
        throw std::runtime_error("backend URL is missing a scheme");
    }
    ParsedUrl parsed;
    parsed.scheme = url.substr(0, scheme_end);
    for (char& character : parsed.scheme) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    std::string rest = url.substr(scheme_end + 3);
    const auto slash = rest.find('/');
    const std::string host_port = slash == std::string::npos ? rest : rest.substr(0, slash);
    parsed.target = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = host_port.rfind(':');
    if (colon != std::string::npos && host_port.find(':') == colon) {
        parsed.host = host_port.substr(0, colon);
        parsed.port = host_port.substr(colon + 1);
    } else {
        parsed.host = host_port;
        parsed.port = parsed.scheme == "https" ? "443" : "80";
    }
    if (parsed.host.empty() || (parsed.scheme != "http" && parsed.scheme != "https")) {
        throw std::runtime_error("backend URL must be http or https");
    }
    return parsed;
}

std::string joinTarget(const ParsedUrl& base, const std::string& path_and_query) {
    std::string path = path_and_query.empty() ? std::string{"/"} : path_and_query;
    if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    if (base.target.empty() || base.target == "/") {
        return path;
    }
    std::string prefix = base.target;
    if (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix + path;
}

class Watchdog {
public:
    template <typename Socket>
    Watchdog(Socket& socket, std::chrono::milliseconds timeout) : thread_([&] {
        std::unique_lock lock(mutex_);
        if (cv_.wait_for(lock, timeout, [&] { return stop_; })) {
            return;
        }
        beast::error_code error;
        socket.cancel(error);
    }) {}

    ~Watchdog() {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::thread thread_;
};

http::verb verbFrom(const std::string& method) {
    const auto verb = http::string_to_verb(method);
    return verb == http::verb::unknown ? http::verb::get : verb;
}

struct IdleConnection {
    std::shared_ptr<net::io_context> ioc;
    std::unique_ptr<beast::tcp_stream> stream;
};

std::string authorityKey(const ParsedUrl& url) {
    return url.scheme + "://" + url.host + ":" + url.port;
}

class HttpPool {
public:
    explicit HttpPool(std::size_t min_connections) : min_connections_(min_connections == 0 ? 1 : min_connections) {
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(ssl::verify_peer);
        SSL_CTX_set_session_cache_mode(ssl_context_.native_handle(), SSL_SESS_CACHE_CLIENT);
    }

    ForwardResult send(const ParsedUrl& url,
                       const ForwardRequest& request,
                       std::chrono::milliseconds connect_timeout,
                       std::chrono::milliseconds read_timeout) {
        if (url.scheme == "https") {
            return sendTls(url, request, connect_timeout, read_timeout);
        }
        return sendHttp(url, request, connect_timeout, read_timeout);
    }

    [[nodiscard]] std::size_t capacity() const { return min_connections_; }
    [[nodiscard]] std::uint64_t reused() const { return reused_.load(); }

private:
    enum class Phase { Connect, Transfer };

    ForwardResult sendHttp(const ParsedUrl& url,
                           const ForwardRequest& request,
                           std::chrono::milliseconds connect_timeout,
                           std::chrono::milliseconds read_timeout) {
        IdleConnection connection = takeIdle(authorityKey(url));
        bool reused = connection.stream != nullptr;
        if (!connection.stream) {
            connection.ioc = std::make_shared<net::io_context>();
            connection.stream = std::make_unique<beast::tcp_stream>(*connection.ioc);
            if (!connectTcp(*connection.stream, url, connect_timeout)) {
                return failed(Phase::Connect);
            }
        } else {
            reused_.fetch_add(1);
        }

        ForwardResult result = exchange(*connection.stream, url, request, read_timeout, reused);
        if (result.kind != ForwardResult::Kind::Ok && reused) {
            IdleConnection fresh;
            fresh.ioc = std::make_shared<net::io_context>();
            fresh.stream = std::make_unique<beast::tcp_stream>(*fresh.ioc);
            if (!connectTcp(*fresh.stream, url, connect_timeout)) {
                return failed(Phase::Connect);
            }
            result = exchange(*fresh.stream, url, request, read_timeout, false);
            connection = std::move(fresh);
        }
        if (result.kind == ForwardResult::Kind::Ok && keepAlive(result)) {
            storeIdle(authorityKey(url), std::move(connection));
        }
        return result;
    }

    ForwardResult sendTls(const ParsedUrl& url,
                          const ForwardRequest& request,
                          std::chrono::milliseconds connect_timeout,
                          std::chrono::milliseconds read_timeout) {
        try {
            auto ioc = std::make_shared<net::io_context>();
            tcp::resolver resolver(*ioc);
            const auto endpoints = resolver.resolve(url.host, url.port);
            beast::ssl_stream<beast::tcp_stream> stream(*ioc, ssl_context_);
            if (!SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str())) {
                return failed(Phase::Connect);
            }
            beast::get_lowest_layer(stream).expires_after(connect_timeout);
            beast::get_lowest_layer(stream).connect(endpoints);
            stream.handshake(ssl::stream_base::client);
            beast::get_lowest_layer(stream).expires_after(read_timeout);
            return writeAndRead(stream, url, request);
        } catch (const beast::system_error& error) {
            return failed(error.code() == beast::error::timeout ? Phase::Transfer : Phase::Connect);
        } catch (const std::exception&) {
            return failed(Phase::Connect);
        }
    }

    template <typename Stream>
    ForwardResult exchange(Stream& stream,
                           const ParsedUrl& url,
                           const ForwardRequest& request,
                           std::chrono::milliseconds read_timeout,
                           bool already_connected) {
        try {
            stream.expires_after(read_timeout);
            return writeAndRead(stream, url, request);
        } catch (const beast::system_error& error) {
            if (already_connected && error.code() != beast::error::timeout) {
                return failed(Phase::Connect);
            }
            return failed(error.code() == beast::error::timeout ? Phase::Transfer : Phase::Connect);
        } catch (const std::exception&) {
            return failed(already_connected ? Phase::Connect : Phase::Transfer);
        }
    }

    template <typename Stream>
    ForwardResult writeAndRead(Stream& stream, const ParsedUrl& url, const ForwardRequest& request) {
        http::request<http::string_body> outbound{verbFrom(request.method), joinTarget(url, request.target), 11};
        outbound.set(http::field::host, url.host);
        outbound.set(http::field::user_agent, "SynapseGateway");
        outbound.set(http::field::connection, "keep-alive");
        for (const HttpHeader& header : request.headers) {
            outbound.set(header.name, header.value);
        }
        outbound.body() = request.body;
        outbound.prepare_payload();
        Watchdog watchdog(beast::get_lowest_layer(stream).socket(), std::chrono::seconds{5});
        http::write(stream, outbound);

        beast::flat_buffer buffer;
        http::response<http::string_body> inbound;
        http::read(stream, buffer, inbound);

        ForwardResult result;
        result.kind = ForwardResult::Kind::Ok;
        result.status = inbound.result_int();
        result.body = inbound.body();
        for (const auto& field : inbound.base()) {
            result.headers.push_back(HttpHeader{std::string(field.name_string()), std::string(field.value())});
        }
        return result;
    }

    static bool connectTcp(beast::tcp_stream& stream, const ParsedUrl& url, std::chrono::milliseconds timeout) {
        try {
            tcp::resolver resolver(stream.get_executor());
            const auto endpoints = resolver.resolve(url.host, url.port);
            Watchdog watchdog(stream.socket(), timeout);
            stream.expires_after(timeout);
            stream.connect(endpoints);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    static bool keepAlive(const ForwardResult& result) {
        for (const HttpHeader& header : result.headers) {
            if (header.name.size() == 10) {
                bool connection = true;
                const char expected[] = "connection";
                for (std::size_t index = 0; index < 10; ++index) {
                    if (std::tolower(static_cast<unsigned char>(header.name[index])) != expected[index]) {
                        connection = false;
                        break;
                    }
                }
                if (connection) {
                    return header.value.find("close") == std::string::npos;
                }
            }
        }
        return true;
    }

    static ForwardResult failed(Phase phase) {
        ForwardResult result;
        result.kind = phase == Phase::Connect ? ForwardResult::Kind::ConnectionFailed : ForwardResult::Kind::Timeout;
        result.status = phase == Phase::Connect ? 502 : 504;
        return result;
    }

    IdleConnection takeIdle(const std::string& key) {
        std::lock_guard lock(mutex_);
        const auto it = idle_.find(key);
        if (it == idle_.end() || it->second.empty()) {
            return {};
        }
        IdleConnection connection = std::move(it->second.back());
        it->second.pop_back();
        return connection;
    }

    void storeIdle(const std::string& key, IdleConnection connection) {
        std::lock_guard lock(mutex_);
        auto& slot = idle_[key];
        if (slot.size() >= min_connections_) {
            return;
        }
        slot.push_back(std::move(connection));
    }

    std::size_t min_connections_;
    ssl::context ssl_context_{ssl::context::tls_client};
    std::mutex mutex_;
    std::unordered_map<std::string, std::vector<IdleConnection>> idle_;
    std::atomic<std::uint64_t> reused_{0};
};

HttpPool& sharedPool(std::size_t min_connections) {
    static HttpPool pool(min_connections);
    return pool;
}

}  // namespace

BackendConnectionPool::BackendConnectionPool(std::size_t min_connections_per_backend)
    : min_connections_(min_connections_per_backend < kMinimumConnections ? kMinimumConnections
                                                                        : min_connections_per_backend) {}

ForwardResult BackendConnectionPool::forward(const std::string& backend_url,
                                            const ForwardRequest& request,
                                            std::chrono::milliseconds connect_timeout,
                                            std::chrono::milliseconds read_timeout) {
    try {
        const ParsedUrl url = parseBackendUrl(backend_url);
        const auto connect = connect_timeout.count() <= 0 ? std::chrono::milliseconds{5000} : connect_timeout;
        const auto read = read_timeout.count() <= 0 ? std::chrono::milliseconds{30000} : read_timeout;
        return sharedPool(min_connections_).send(url, request, connect, read);
    } catch (const std::exception&) {
        ForwardResult result;
        result.kind = ForwardResult::Kind::ConnectionFailed;
        result.status = 502;
        return result;
    }
}

std::size_t BackendConnectionPool::poolCapacity(const std::string&) const {
    return min_connections_;
}

std::uint64_t BackendConnectionPool::reusedConnections() const {
    return sharedPool(min_connections_).reused();
}

}  // namespace gateway::core
