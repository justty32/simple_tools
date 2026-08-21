#define _POSIX_C_SOURCE 200809L

#include <aos/exec.hpp>

#include <catch2/catch_test_macros.hpp>

#include "exec_test_support.hpp"

#include <cstdlib>

TEST_CASE("execute redirects standard streams and truncates output") {
    TempDir dir;
    const std::string input = dir.path + "/input";
    const std::string output = dir.path + "/output";
    write_file(input, "new content\n");
    write_file(output, "old content that must disappear\n");

    aos::inst_t inst = command({"/bin/cat"});
    inst.stdin_path = input;
    inst.stdout_path = output;
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.status == 0);
    CHECK_FALSE(result.signalled);
    CHECK(read_file(output) == "new content\n");
}

TEST_CASE("execute redirects stderr") {
    TempDir dir;
    aos::inst_t inst = command({"/bin/sh", "-c", "printf error >&2"});
    inst.stderr_path = dir.path + "/stderr";
    write_file(inst.stderr_path, "old error with trailing data");
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(read_file(inst.stderr_path) == "error");
}

TEST_CASE("execute applies cwd and extends inherited environment") {
    TempDir dir;
    REQUIRE(setenv("AOS_CPP_PARENT", "inherited", 1) == 0);

    char original_cwd[4096];
    REQUIRE(getcwd(original_cwd, sizeof(original_cwd)) != nullptr);
    std::string expected_cwd = original_cwd;
    if (expected_cwd.back() != '/') {
        expected_cwd += '/';
    }
    expected_cwd += dir.path;

    aos::inst_t inst = command(
        {"/bin/sh", "-c", "printf '%s:%s:' \"$AOS_CPP_NEW\" \"$AOS_CPP_PARENT\"; pwd"});
    inst.cwd = dir.path;
    inst.env["AOS_CPP_NEW"] = "added";
    inst.stdout_path = dir.path + "/environment";
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(read_file(inst.stdout_path) ==
          "added:inherited:" + expected_cwd + "\n");
    CHECK(unsetenv("AOS_CPP_PARENT") == 0);
}

TEST_CASE("execute overrides an inherited environment value") {
    TempDir dir;
    REQUIRE(setenv("AOS_CPP_OVERRIDE", "old", 1) == 0);

    aos::inst_t inst = command({"/bin/sh", "-c", "printf %s \"$AOS_CPP_OVERRIDE\""});
    inst.env["AOS_CPP_OVERRIDE"] = "new";
    inst.stdout_path = dir.path + "/environment";
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(read_file(inst.stdout_path) == "new");
    CHECK(unsetenv("AOS_CPP_OVERRIDE") == 0);
}

TEST_CASE("execute inherits the environment when no overrides are present") {
    TempDir dir;
    REQUIRE(setenv("AOS_CPP_INHERIT_ONLY", "present", 1) == 0);

    aos::inst_t inst =
        command({"/bin/sh", "-c", "printf %s \"$AOS_CPP_INHERIT_ONLY\""});
    inst.stdout_path = dir.path + "/environment";
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(read_file(inst.stdout_path) == "present");
    CHECK(unsetenv("AOS_CPP_INHERIT_ONLY") == 0);
}
