#include "gateway/core/backend_client.h"

#include "../support/local_http.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

TEST(BackendPoolTest, KeepsAtLeastTenSlotsAndReusesConnections) {
    gateway::test::LocalHttpServer server([](const gateway::test::LocalRequest& request) {
        gateway::test::LocalResponse response;
        response.status = 200;
        response.body = request.target;
        response.content_type = "text/plain";
        return response;
    });
    gateway::core::BackendConnectionPool pool(10);
    EXPECT_GE(pool.poolCapacity("http://127.0.0.1/"), 10u);

    const std::string url = "http://127.0.0.1:" + std::to_string(server.port());
    const auto before = pool.reusedConnections();
    for (int index = 0; index < 6; ++index) {
        gateway::core::ForwardRequest request;
        request.method = "GET";
        request.target = "/pool";
        const auto result = pool.forward(url, request, std::chrono::seconds{2}, std::chrono::seconds{2});
        ASSERT_EQ(result.kind, gateway::core::ForwardResult::Kind::Ok);
        EXPECT_EQ(result.status, 200);
        EXPECT_EQ(result.body, "/pool");
    }
    EXPECT_GT(pool.reusedConnections(), before);
    server.stop();
}
