#include <aos/format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("write_one emits one compact LF-terminated record") {
    aos::inst_t inst;
    inst.argv = {"echo", "hello"};
    inst.stdout_path = "/tmp/out";
    inst.env = {{"LANG", "C"}};
    inst.timeout_ms = 25;
    std::string out = "prefix:";

    REQUIRE(aos::write_one(inst, out) == aos::InstState::Ok);
    CHECK(out ==
          "prefix:{\"argv\":[\"echo\",\"hello\"],\"stdout\":\"/tmp/out\","
          "\"env\":{\"LANG\":\"C\"},\"timeout_ms\":25}\n");
}

TEST_CASE("write_one output round trips through read_one") {
    aos::inst_t original;
    original.argv = {"printf", "%s", "value"};
    original.stdin_path = "input";
    original.stderr_path = "errors";
    original.exit_path = "status";
    original.cwd = "/var/tmp";
    original.env = {{"A", "line\nvalue"}, {"B", "="}};
    original.timeout_ms = 1234;
    std::string encoded;

    REQUIRE(aos::write_one(original, encoded) == aos::InstState::Ok);
    REQUIRE(encoded.back() == '\n');
    aos::inst_t decoded;
    REQUIRE(aos::read_one(encoded.data(), encoded.size() - 1, decoded) ==
            aos::InstState::Ok);
    CHECK(decoded.argv == original.argv);
    CHECK(decoded.stdin_path == original.stdin_path);
    CHECK(decoded.stdout_path == original.stdout_path);
    CHECK(decoded.stderr_path == original.stderr_path);
    CHECK(decoded.exit_path == original.exit_path);
    CHECK(decoded.cwd == original.cwd);
    CHECK(decoded.env == original.env);
    CHECK(decoded.timeout_ms == original.timeout_ms);
}

TEST_CASE("write_one leaves output unchanged after validation failure") {
    aos::inst_t invalid;
    invalid.argv = {""};
    std::string out = "unchanged";

    CHECK(aos::write_one(invalid, out) == aos::InstState::EmptyArgv);
    CHECK(out == "unchanged");
}
