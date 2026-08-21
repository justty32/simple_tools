#include <aos/format.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

TEST_CASE("read_one parses every instruction field") {
    const std::string input =
        R"({"argv":["sh","-c","echo hi"],"stdin":"in","stdout":"out","stderr":"err","exit":"rc","cwd":"/tmp","env":{"A":"1","B":"two"},"timeout_ms":5000})";
    aos::inst_t inst;

    REQUIRE(aos::read_one(input.data(), input.size(), inst) == aos::InstState::Ok);
    REQUIRE(inst.argv == std::vector<std::string>{"sh", "-c", "echo hi"});
    CHECK(inst.stdin_path == "in");
    CHECK(inst.stdout_path == "out");
    CHECK(inst.stderr_path == "err");
    CHECK(inst.exit_path == "rc");
    CHECK(inst.cwd == "/tmp");
    CHECK(inst.env == std::map<std::string, std::string>{{"A", "1"}, {"B", "two"}});
    CHECK(inst.timeout_ms == 5000);
}

TEST_CASE("read_all skips blank lines while retaining physical line numbers") {
    const std::string input =
        "{\"argv\":[\"first\"]}\r\n\r\n{\"argv\":[\"second\"]}\n";
    std::vector<aos::inst_t> out;
    std::size_t error_line = 99;

    REQUIRE(aos::read_all(input.data(), input.size(), out, &error_line) ==
            aos::InstState::Ok);
    REQUIRE(out.size() == 2);
    CHECK(out[0].argv == std::vector<std::string>{"first"});
    CHECK(out[1].argv == std::vector<std::string>{"second"});
    CHECK(error_line == 0);
    CHECK(out[0].stdin_path.empty());
    CHECK(out[0].env.empty());
    CHECK(out[0].timeout_ms == 0);
}

TEST_CASE("read_all is atomic and reports a one-based line number") {
    const std::string input =
        "{\"argv\":[\"first\"]}\n\n{\"argv\":[]}\n";
    std::vector<aos::inst_t> out(1);
    std::size_t error_line = 0;

    CHECK(aos::read_all(input.data(), input.size(), out, &error_line) ==
          aos::InstState::EmptyArgv);
    CHECK(out.empty());
    CHECK(error_line == 3);
}
