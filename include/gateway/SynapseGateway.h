#pragma once

namespace gateway {

/**
 * @brief Process entry for the SynapseGateway reverse proxy.
 *
 * The executable loads a TOML configuration, opens the payment and identity
 * services, and serves HTTP until the process is asked to stop.
 */
class SynapseGateway {
public:
    [[nodiscard]] static int run(int argc, char** argv);
};

}  // namespace gateway
