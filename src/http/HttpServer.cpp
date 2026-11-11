#define _CRT_SECURE_NO_WARNINGS

#include "gateway/http/HttpServer.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <drogon/drogon.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gateway::http {
namespace {

std::atomic<HttpServer*> active_server{nullptr};

void onPosixSignal(int) {
    if (HttpServer* server = active_server.load()) {
        server->notifySignal();
    }
}

#ifdef _WIN32
BOOL WINAPI onConsoleSignal(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        if (HttpServer* server = active_server.load()) {
            server->notifySignal();
        }
        return TRUE;
    }
    return FALSE;
}
#endif

drogon::HttpResponsePtr toResponse(const gateway::core::GatewayResponse& result) {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(result.status));
    response->setBody(result.body);
    if (!result.content_type.empty()) {
        response->setContentTypeString(result.content_type);
    }
    for (const auto& header : result.headers) {
        if (header.name.size() == 12) {
            bool content_type = true;
            const char expected[] = "content-type";
            for (std::size_t index = 0; index < 12; ++index) {
                const auto character = static_cast<unsigned char>(header.name[index]);
                if (std::tolower(character) != static_cast<unsigned char>(expected[index])) {
                    content_type = false;
                    break;
                }
            }
            if (content_type) {
                continue;
            }
        }
        response->addHeader(header.name, header.value);
    }
    return response;
}

}  // namespace

struct HttpServer::Impl {
    gateway::core::GatewayRouter& router;
    std::string listen_address;
    std::uint16_t port = 0;
    std::uint32_t threads = 2;
    std::atomic<bool> signal_stop{false};
    std::atomic<bool> stopping{false};

    Impl(gateway::core::GatewayRouter& router_in, std::string address, std::uint16_t port_in, std::uint32_t threads_in)
        : router(router_in), listen_address(std::move(address)), port(port_in), threads(threads_in < 2 ? 2 : threads_in) {}

    drogon::Task<drogon::HttpResponsePtr> handleRequest(drogon::HttpRequestPtr request) {
        gateway::core::ForwardRequest incoming;
        incoming.method = request->methodString();
        incoming.target = request->path();
        if (!request->query().empty()) {
            incoming.target.push_back('?');
            incoming.target += request->query();
        }
        incoming.body = std::string(request->getBody());
        for (const auto& header : request->headers()) {
            incoming.headers.push_back(gateway::core::HttpHeader{header.first, header.second});
        }
        co_return toResponse(router.handle(incoming));
    }
};

HttpServer::HttpServer(gateway::core::GatewayRouter& router,
                       std::string listen_address,
                       std::uint16_t port,
                       std::uint32_t threads)
    : impl_(std::unique_ptr<Impl>(new Impl(router, std::move(listen_address), port, threads))) {}

HttpServer::~HttpServer() {
    if (active_server.load() == this) {
        active_server.store(nullptr);
    }
}

void HttpServer::notifySignal() {
    impl_->signal_stop.store(true);
}

void HttpServer::requestStop() {
    if (impl_->stopping.exchange(true)) {
        return;
    }
    impl_->router.shutdown(std::chrono::seconds{30});
    drogon::app().quit();
}

void HttpServer::run() {
    active_server.store(this);
    std::signal(SIGTERM, onPosixSignal);
    std::signal(SIGINT, onPosixSignal);
#ifdef _WIN32
    SetConsoleCtrlHandler(onConsoleSignal, TRUE);
#endif

    auto& app = drogon::app();
    app.setThreadNum(impl_->threads);
    app.addListener(impl_->listen_address, impl_->port);
    app.registerBeginningAdvice([this] {
        drogon::app().getLoop()->runEvery(0.2, [this] {
            if (impl_->signal_stop.exchange(false)) {
                requestStop();
            }
        });
    });
    app.registerHandlerViaRegex(
        ".*",
        [this](drogon::HttpRequestPtr request) -> drogon::Task<drogon::HttpResponsePtr> {
            co_return co_await impl_->handleRequest(std::move(request));
        });
    app.run();
    if (active_server.load() == this) {
        active_server.store(nullptr);
    }
}

}  // namespace gateway::http
