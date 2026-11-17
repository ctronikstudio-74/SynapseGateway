#include "gateway/SynapseGateway.h"

#include "gateway/blockchain/BlockchainConnector.h"
#include "gateway/blockchain/network_monitor.h"
#include "gateway/blockchain/rpc_client.h"
#include "gateway/core/ConfigurationManager.h"
#include "gateway/core/GatewayRouter.h"
#include "gateway/core/IdentityVerifier.h"
#include "gateway/core/PaymentProcessor.h"
#include "gateway/core/ReputationChecker.h"
#include "gateway/core/backend_client.h"
#include "gateway/http/HttpServer.h"
#include "gateway/monitoring/MetricsCollector.h"
#include "gateway/monitoring/otlp_span.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

std::filesystem::path statePath() {
    return std::filesystem::path("data") / "payment-state.json";
}

}  // namespace

int gateway::SynapseGateway::run(int argc, char** argv) {
    const bool check_only = argc > 1 && std::string_view(argv[1]) == "check";
    const std::filesystem::path config_path = check_only ? (argc > 2 ? std::filesystem::path(argv[2])
                                                                    : std::filesystem::path("config") / "gateway.dev.toml")
                                                        : (argc > 1 ? std::filesystem::path(argv[1])
                                                                    : std::filesystem::path("config") / "gateway.dev.toml");
    if (check_only) {
        bool valid = false;
        const std::string report = gateway::core::ConfigurationManager::checkFile(config_path, valid);
        if (valid) {
            std::cout << report;
            return 0;
        }
        std::cerr << report << std::endl;
        return 1;
    }
    const auto loaded = gateway::core::ConfigurationManager::loadFromFile(config_path);
    if (!loaded) {
        std::cerr << loaded.error() << std::endl;
        return 1;
    }
    const auto errors = gateway::core::ConfigurationManager::validate(loaded.value());
    if (!errors.empty()) {
        for (const auto& error : errors) {
            std::cerr << error << std::endl;
        }
        return 1;
    }

    auto configuration = std::make_shared<gateway::core::Configuration>(loaded.value());
    gateway::monitoring::Logger::initialize(configuration->monitoring.log_level);

    auto rpc = std::make_shared<gateway::blockchain::HttpRpcClient>();
    auto connector = std::make_shared<gateway::blockchain::BlockchainConnector>(
        configuration->networks, configuration->payment_wallet_seed, rpc, configuration->treasury_private_key);
    connector->initialize();
    gateway::blockchain::NetworkMonitor monitor(connector, std::chrono::seconds{10}, std::chrono::seconds{60});

    auto payment = std::make_shared<gateway::core::PaymentProcessor>(connector, configuration, statePath());
    auto identity = std::make_shared<gateway::core::IdentityVerifier>(connector);
    auto reputation = std::make_shared<gateway::core::ReputationChecker>(connector, configuration);
    auto metrics = std::make_shared<gateway::monitoring::MetricsCollector>();
    if (configuration->monitoring.prometheus_enabled && configuration->server.monitoring_port != 0) {
        metrics->initialize(configuration->server.monitoring_port);
    }
    auto backend = std::make_shared<gateway::core::BackendConnectionPool>();
    gateway::core::GatewayRouter router(configuration, payment, identity, reputation, connector, backend, metrics);
    if (configuration->monitoring.opentelemetry_enabled && !configuration->monitoring.otlp_endpoint.empty()) {
        router.setSpanExporter(std::make_shared<gateway::monitoring::OtlpHttpExporter>(backend, configuration->monitoring.otlp_endpoint));
    } else if (configuration->monitoring.opentelemetry_enabled) {
        gateway::monitoring::Logger::warn("opentelemetry_enabled is set and otlp_endpoint is empty; spans stay local");
    }

    auto watcher = gateway::core::ConfigurationManager::watchForChanges(
        config_path, [&](gateway::core::Configuration updated) {
            auto next = std::make_shared<gateway::core::Configuration>(std::move(updated));
            router.applyConfiguration(std::move(next));
        });

    gateway::monitoring::Logger::info("SynapseGateway listening, payment wallet " + connector->paymentWallet());
    gateway::http::HttpServer server(router, configuration->server.listen_address, configuration->server.listen_port,
                                     configuration->server.thread_pool_size);
    server.run();
    return 0;
}

int main(int argc, char** argv) {
    return gateway::SynapseGateway::run(argc, argv);
}
