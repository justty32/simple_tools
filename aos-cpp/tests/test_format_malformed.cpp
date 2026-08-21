#include <aos/format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

aos::InstState parse(const std::string &input) {
    aos::inst_t out;
    return aos::read_one(input.data(), input.size(), out);
}

}  // namespace

TEST_CASE("malformed JSON is rejected") {
    CHECK(parse(R"({"argv":["x"])") == aos::InstState::JsonSyntax);
}

TEST_CASE("valid non-object JSON is rejected") {
    CHECK(parse(R"(["x"])") == aos::InstState::NotAnObject);
}

TEST_CASE("unknown schema keys are rejected") {
    CHECK(parse(R"({"argv":["x"],"stdou":"file"})") ==
          aos::InstState::UnknownKey);
}

TEST_CASE("adversarially deep JSON is rejected during parsing") {
    CHECK(parse(R"({"argv":["x"],"env":{"A":{"B":{"C":[]}}}})") ==
          aos::InstState::DepthExceeded);
}

TEST_CASE("field type mismatches are rejected") {
    CHECK(parse(R"({"argv":"x"})") == aos::InstState::FieldTypeMismatch);
    CHECK(parse(R"({"argv":["x",1]})") == aos::InstState::FieldTypeMismatch);
    CHECK(parse(R"({"argv":["x"],"cwd":false})") ==
          aos::InstState::FieldTypeMismatch);
    CHECK(parse(R"({"argv":["x"],"env":[]})") ==
          aos::InstState::FieldTypeMismatch);
    CHECK(parse(R"({"argv":["x"],"env":{"A":1}})") ==
          aos::InstState::FieldTypeMismatch);
    CHECK(parse(R"({"argv":["x"],"timeout_ms":-1})") ==
          aos::InstState::FieldTypeMismatch);
}

TEST_CASE("missing or unusable argv is rejected") {
    CHECK(parse(R"({})") == aos::InstState::EmptyArgv);
    CHECK(parse(R"({"argv":[]})") == aos::InstState::EmptyArgv);
    CHECK(parse(R"({"argv":[""]})") == aos::InstState::EmptyArgv);
}

TEST_CASE("invalid environment keys are rejected") {
    CHECK(parse(R"({"argv":["x"],"env":{"":"value"}})") ==
          aos::InstState::EnvKeyInvalid);
    CHECK(parse(R"({"argv":["x"],"env":{"A=B":"value"}})") ==
          aos::InstState::EnvKeyInvalid);
}

TEST_CASE("null input pointers are rejected") {
    aos::inst_t out;
    CHECK(aos::read_one(nullptr, 0, out) == aos::InstState::InvalidArgument);
}
