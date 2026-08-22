#include <aos/format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

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

TEST_CASE("write_all emits one compact LF-terminated array") {
    aos::inst_t first;
    first.argv = {"first"};
    aos::inst_t second;
    second.argv = {"second"};
    second.cwd = "/tmp";
    std::string out = "prefix:";
    std::size_t error_record = 99;

    REQUIRE(aos::write_all({first, second}, out, &error_record) ==
            aos::InstState::Ok);
    CHECK(out ==
          "prefix:[{\"argv\":[\"first\"]},"
          "{\"argv\":[\"second\"],\"cwd\":\"/tmp\"}]\n");
    CHECK(error_record == 0);
}

TEST_CASE("write_all emits an empty array for an empty batch") {
    std::string out;
    REQUIRE(aos::write_all({}, out, nullptr) == aos::InstState::Ok);
    CHECK(out == "[]\n");
}

TEST_CASE("write_all output round trips through read_all") {
    aos::inst_t first;
    first.argv = {"printf", "%s", "value"};
    first.env = {{"A", "line\nvalue"}};
    first.timeout_ms = 1234;
    aos::inst_t second;
    second.argv = {"cat"};
    second.stdin_path = "input";
    second.exit_path = "status";
    const std::vector<aos::inst_t> originals = {first, second};
    std::string encoded;

    REQUIRE(aos::write_all(originals, encoded, nullptr) == aos::InstState::Ok);
    std::vector<aos::inst_t> decoded;
    REQUIRE(aos::read_all(encoded.data(), encoded.size(), decoded, nullptr) ==
            aos::InstState::Ok);
    REQUIRE(decoded.size() == originals.size());
    for (std::size_t i = 0; i < originals.size(); ++i) {
        CHECK(decoded[i].argv == originals[i].argv);
        CHECK(decoded[i].stdin_path == originals[i].stdin_path);
        CHECK(decoded[i].exit_path == originals[i].exit_path);
        CHECK(decoded[i].env == originals[i].env);
        CHECK(decoded[i].timeout_ms == originals[i].timeout_ms);
    }
}

TEST_CASE("write_all is atomic and reports a one-based record number") {
    aos::inst_t valid;
    valid.argv = {"valid"};
    aos::inst_t invalid;
    invalid.env = {{"", "value"}};
    invalid.argv = {"invalid"};
    std::string out = "unchanged";
    std::size_t error_record = 0;

    CHECK(aos::write_all({valid, invalid}, out, &error_record) ==
          aos::InstState::EnvKeyInvalid);
    CHECK(out == "unchanged");
    CHECK(error_record == 2);
}
