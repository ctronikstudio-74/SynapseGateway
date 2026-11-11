#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gateway::test {

struct LocalRequest {
    std::string method;
    std::string target;
    std::string body;
    std::vector<std::pair<std::string, std::string>> headers;
};

struct LocalResponse {
    int status = 200;
    std::string body;
    std::string content_type = "text/plain";
};

class LocalHttpServer {
public:
    using Handler = std::function<LocalResponse(const LocalRequest&)>;

    explicit LocalHttpServer(Handler handler) : handler_(std::move(handler)) {
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), 0);
        acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(ioc_, endpoint);
        port_ = acceptor_->local_endpoint().port();
        thread_ = std::thread([this] { run(); });
    }

    ~LocalHttpServer() { stop(); }

    LocalHttpServer(const LocalHttpServer&) = delete;
    LocalHttpServer& operator=(const LocalHttpServer&) = delete;

    [[nodiscard]] std::uint16_t port() const { return port_; }

    void stop() {
        if (stopped_.exchange(true)) {
            return;
        }
        boost::system::error_code error;
        if (acceptor_) {
            acceptor_->close(error);
        }
        {
            std::lock_guard lock(socket_mutex_);
            if (active_ != nullptr) {
                active_->close(error);
            }
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void run() {
        namespace beast = boost::beast;
        namespace http = beast::http;
        using tcp = boost::asio::ip::tcp;
        while (!stopped_.load() && acceptor_ && acceptor_->is_open()) {
            tcp::socket socket(ioc_);
            boost::system::error_code error;
            acceptor_->accept(socket, error);
            if (error || stopped_.load()) {
                break;
            }
            beast::tcp_stream stream(std::move(socket));
            {
                std::lock_guard lock(socket_mutex_);
                active_ = &stream.socket();
            }
            beast::flat_buffer buffer;
            while (!stopped_.load()) {
                stream.expires_after(std::chrono::seconds{3});
                http::request<http::string_body> request;
                http::read(stream, buffer, request, error);
                if (error) {
                    break;
                }
                LocalRequest incoming;
                incoming.method = std::string(request.method_string());
                incoming.target = std::string(request.target());
                incoming.body = request.body();
                for (const auto& field : request.base()) {
                    incoming.headers.emplace_back(std::string(field.name_string()), std::string(field.value()));
                }
                const LocalResponse produced = handler_ ? handler_(incoming) : LocalResponse{};
                http::response<http::string_body> response{static_cast<http::status>(produced.status), request.version()};
                response.set(http::field::content_type, produced.content_type);
                response.set(http::field::connection, "keep-alive");
                response.body() = produced.body;
                response.prepare_payload();
                http::write(stream, response, error);
                if (error || !request.keep_alive()) {
                    break;
                }
            }
            {
                std::lock_guard lock(socket_mutex_);
                active_ = nullptr;
            }
        }
    }

    boost::asio::io_context ioc_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    boost::asio::ip::tcp::socket* active_ = nullptr;
    std::mutex socket_mutex_;
    Handler handler_;
    std::uint16_t port_ = 0;
    std::atomic<bool> stopped_{false};
    std::thread thread_;
};

}  // namespace gateway::test
