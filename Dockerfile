# Multi-stage build. The runtime image keeps the proxy, the example config, and the shared libraries vcpkg produced.
FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake curl git ninja-build pkg-config tar unzip zip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt
RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_TARGET_TRIPLET=x64-linux \
    && cmake --build build --target gateway_node

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /var/lib/agentgateway agentgateway

COPY --from=build /src/build/gateway_node /usr/local/bin/gateway_node
COPY --from=build /opt/vcpkg/installed/x64-linux/lib/ /usr/local/lib/
COPY config/gateway.example.toml /etc/agentgateway/gateway.toml
RUN ldconfig \
    && mkdir -p /var/lib/agentgateway/data \
    && chown -R agentgateway:agentgateway /var/lib/agentgateway /etc/agentgateway

USER agentgateway
WORKDIR /var/lib/agentgateway
EXPOSE 8080 9090

HEALTHCHECK --interval=30s --timeout=3s --start-period=20s --retries=3 \
    CMD curl --fail --silent http://127.0.0.1:8080/health || exit 1

ENTRYPOINT ["/usr/local/bin/gateway_node"]
CMD ["/etc/agentgateway/gateway.toml"]
