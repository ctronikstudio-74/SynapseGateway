#pragma once

#include "gateway/core/GatewayRouter.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gateway::http {

class HttpServer {
public:
    HttpServer(gateway::core::GatewayRouter& router, std::string listen_address, std::uint16_t port, std::uint32_t threads);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void run();
    void requestStop();
    void notifySignal();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gateway::http
