#include "synapse_gateway/client.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace synapse::gateway {
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
        throw std::invalid_argument("SDK URL is missing a scheme");
    }
    ParsedUrl parsed;
    parsed.scheme = url.substr(0, scheme_end);
    const std::string rest = url.substr(scheme_end + 3);
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
        throw std::invalid_argument("SDK URL must be http or https");
    }
    return parsed;
}

HttpResponse fromMessage(const http::response<http::string_body>& message) {
    HttpResponse result;
    result.status = static_cast<int>(message.result_int());
    result.body = message.body();
    for (const auto& field : message.base()) {
        result.headers.emplace_back(std::string(field.name_string()), std::string(field.value()));
    }
    return result;
}

template <typename Stream>
HttpResponse post(Stream& stream, const ParsedUrl& url, const std::string& method, const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& headers) {
    http::request<http::string_body> request{http::string_to_verb(method), url.target, 11};
    if (request.method() == http::verb::unknown) {
        throw std::invalid_argument("unsupported HTTP method");
    }
    request.set(http::field::host, url.host);
    request.set(http::field::user_agent, "SynapseGateway-SDK/0.3.0");
    request.set(http::field::accept, "application/json");
    for (const auto& [name, value] : headers) {
        request.set(name, value);
    }
    request.body() = body;
    request.prepare_payload();
    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    return fromMessage(response);
}

class HttpTransport final : public Transport {
public:
    HttpResponse exchange(const std::string& method,
                          const std::string& url,
                          const std::string& body,
                          const std::vector<std::pair<std::string, std::string>>& headers) override {
        const ParsedUrl parsed = parseUrl(url);
        net::io_context context;
        tcp::resolver resolver(context);
        const auto endpoints = resolver.resolve(parsed.host, parsed.port);
        if (parsed.scheme == "http") {
            beast::tcp_stream stream(context);
            stream.expires_after(std::chrono::seconds(30));
            stream.connect(endpoints);
            const HttpResponse response = post(stream, parsed, method, body, headers);
            beast::error_code error;
            stream.socket().shutdown(tcp::socket::shutdown_both, error);
            (void)error;
            return response;
        }
        ssl::context ssl_context{ssl::context::tls_client};
        ssl_context.set_default_verify_paths();
        ssl_context.set_verify_mode(ssl::verify_peer);
        beast::ssl_stream<beast::tcp_stream> stream(context, ssl_context);
        if (SSL_set_tlsext_host_name(stream.native_handle(), parsed.host.c_str()) != 1) {
            throw std::runtime_error("failed to set TLS server name");
        }
        beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
        beast::get_lowest_layer(stream).connect(endpoints);
        stream.handshake(ssl::stream_base::client);
        const HttpResponse response = post(stream, parsed, method, body, headers);
        beast::error_code error;
        stream.shutdown(error);
        (void)error;
        return response;
    }
};

}  // namespace

std::shared_ptr<Transport> makeHttpTransport() {
    return std::make_shared<HttpTransport>();
}

}  // namespace synapse::gateway
