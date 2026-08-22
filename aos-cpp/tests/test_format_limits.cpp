#include <aos/format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("JSON depth is stopped during parsing") {
    const std::string input =
        R"({"argv":["echo"],"env":{"A":{"B":[]}}})";
    aos::inst_t out;
    CHECK(aos::read_one(input.data(), input.size(), out) ==
          aos::InstState::DepthExceeded);
}

TEST_CASE("argv element limit is enforced") {
    std::string input = "{\"argv\":[";
    for (std::size_t i = 0; i < aos::kMaxArgs + 1; ++i) {
        if (i != 0) input += ',';
        input += "\"x\"";
    }
    input += "]}";
    aos::inst_t out;
    CHECK(aos::read_one(input.data(), input.size(), out) ==
          aos::InstState::TooManyArgs);
}

TEST_CASE("environment entry limit is enforced") {
    std::string input = "{\"argv\":[\"x\"],\"env\":{";
    for (std::size_t i = 0; i < aos::kMaxEnv + 1; ++i) {
        if (i != 0) input += ',';
        input += "\"K" + std::to_string(i) + "\":\"v\"";
    }
    input += "}}";
    aos::inst_t out;
    CHECK(aos::read_one(input.data(), input.size(), out) ==
          aos::InstState::TooManyEnv);
}
