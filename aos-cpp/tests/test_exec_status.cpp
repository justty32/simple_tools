#define _POSIX_C_SOURCE 200809L

#include <aos/exec.hpp>

#include <catch2/catch_test_macros.hpp>

#include "exec_test_support.hpp"

TEST_CASE("execute reports setup and exec failures as child statuses") {
    TempDir dir;

    SECTION("redirection failure is 126") {
        aos::inst_t inst = command({"/bin/echo", "unreachable"});
        inst.stdout_path = dir.path + "/missing/output";
        inst.exit_path = dir.path + "/exit";
        aos::ExecResult result;

        REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
        CHECK(result.status == 126);
        CHECK(read_file(inst.exit_path) == "126\n");
    }

    SECTION("cwd failure is 126") {
        aos::inst_t inst = command({"/bin/echo", "unreachable"});
        inst.cwd = dir.path + "/missing";
        aos::ExecResult result;

        REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
        CHECK(result.status == 126);
    }

    SECTION("environment setup failure is 126") {
        aos::inst_t inst = command({"/bin/echo", "unreachable"});
        inst.env[""] = "invalid";
        aos::ExecResult result;

        REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
        CHECK(result.status == 126);
    }

    SECTION("exec failure is 127") {
        aos::inst_t inst = command({"aos-cpp-command-that-does-not-exist"});
        inst.exit_path = dir.path + "/exit";
        aos::ExecResult result;

        REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
        CHECK(result.status == 127);
        CHECK(read_file(inst.exit_path) == "127\n");
    }
}

TEST_CASE("execute records normal and signalled termination") {
    TempDir dir;

    SECTION("normal nonzero exit") {
        aos::inst_t inst = command({"/bin/sh", "-c", "exit 3"});
        inst.exit_path = dir.path + "/exit";
        write_file(inst.exit_path, "previous status and trailing data\n");
        aos::ExecResult result;

        REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
        CHECK(result.status == 3);
        CHECK_FALSE(result.signalled);
        CHECK(read_file(inst.exit_path) == "3\n");
    }

    SECTION("signal exit") {
        aos::inst_t inst = command({"/bin/sh", "-c", "kill -TERM $$"});
        inst.exit_path = dir.path + "/exit";
        aos::ExecResult result;

        REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
        CHECK(result.status == 143);
        CHECK(result.signalled);
        CHECK(read_file(inst.exit_path) == "143\n");
    }
}

TEST_CASE("execute validates argv and resets its result") {
    SECTION("empty argv") {
        aos::inst_t inst;
        aos::ExecResult result{99, true, true, 42};

        CHECK(aos::execute(inst, result) == aos::ExecState::InvalidArgument);
        CHECK(result.status == 0);
        CHECK_FALSE(result.signalled);
        CHECK_FALSE(result.timed_out);
        CHECK(result.error == 0);
    }

    SECTION("empty argv zero") {
        aos::inst_t inst = command({""});
        aos::ExecResult result;

        CHECK(aos::execute(inst, result) == aos::ExecState::InvalidArgument);
    }
}

TEST_CASE("execute reports an unwritable exit status file") {
    TempDir dir;
    aos::inst_t inst = command({"/bin/sh", "-c", "exit 4"});
    inst.exit_path = dir.path + "/missing/exit";
    aos::ExecResult result;

    CHECK(aos::execute(inst, result) == aos::ExecState::ExitWriteFailed);
    CHECK(result.status == 4);
}

TEST_CASE("exec state strings cover every declared state") {
    const aos::ExecState states[] = {
        aos::ExecState::Ok,
        aos::ExecState::InvalidArgument,
        aos::ExecState::SpawnFailed,
        aos::ExecState::WaitFailed,
        aos::ExecState::ExitWriteFailed,
    };
    for (aos::ExecState state : states) {
        CHECK(std::string(aos::to_string(state)).empty() == false);
    }
    CHECK(std::string(aos::to_string(static_cast<aos::ExecState>(999))).empty() ==
          false);
}
