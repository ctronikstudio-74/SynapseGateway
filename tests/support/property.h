#pragma once

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <string_view>

namespace gateway::test {

inline constexpr int kPropertyIterations = 100;

template <typename Generator>
void checkProperty(std::string_view name, Generator&& generator) {
    std::mt19937 rng(0x8004u);
    for (int iteration = 0; iteration < kPropertyIterations; ++iteration) {
        SCOPED_TRACE(std::string(name) + " iteration " + std::to_string(iteration));
        generator(rng);
    }
}

}  // namespace gateway::test
