#include <aos/format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("record byte limit is enforced") {
    const std::string input = R"({"argv":["echo"]})";
    aos::ReadOptions opts;
    opts.max_record_bytes = input.size() - 1;
    aos::inst_t out;
    CHECK(aos::read_one(input.data(), input.size(), out, opts) ==
          aos::InstState::RecordTooLong);
}

TEST_CASE("total byte limit is enforced before parsing") {
    const std::string input = "{\"argv\":[\"echo\"]}\n";
    aos::ReadOptions opts;
    opts.max_total_bytes = input.size() - 1;
    std::vector<aos::inst_t> out;
    CHECK(aos::read_all(input.data(), input.size(), out, nullptr, opts) ==
          aos::InstState::TotalTooLong);
    CHECK(out.empty());
}

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
