#define _POSIX_C_SOURCE 200809L

#include <aos/exec.hpp>

#include <catch2/catch_test_macros.hpp>

#include "exec_test_support.hpp"

TEST_CASE("execute searches PATH for argv zero") {
    TempDir dir;
    aos::inst_t inst = command({"echo", "via path"});
    inst.stdout_path = dir.path + "/stdout";
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.status == 0);
    CHECK(read_file(inst.stdout_path) == "via path\n");
}

TEST_CASE("execute searches the command environment PATH") {
    TempDir dir;
    const std::string program = dir.path + "/aos-custom-path-command";
    write_file(program, "#!/bin/sh\nprintf custom-path");
    REQUIRE(chmod(program.c_str(), 0700) == 0);

    aos::inst_t inst = command({"aos-custom-path-command"});
    inst.env["PATH"] = dir.path;
    inst.stdout_path = dir.path + "/stdout";
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.status == 0);
    CHECK(read_file(inst.stdout_path) == "custom-path");
}

/*
 * 有些檔案系統不保存 Unix 權限位元 —— WSL 底下的 /mnt/c 就是一例：chmod 回報
 * 成功，檔案卻仍然是可執行的。接下來兩個測試檢驗的正是「找到了但不可執行」，
 * 在那種檔案系統上無法成立，所以先探測一次再決定要不要跑。跳過而不是放寬斷言：
 * 這兩條在真正的 Linux 檔案系統上該有多嚴格就多嚴格。
 */
static bool executable_bit_is_honoured(const std::string &dir) {
    const std::string probe = dir + "/aos-perm-probe";
    struct stat st;

    write_file(probe, "x");
    if (chmod(probe.c_str(), 0600) != 0 || stat(probe.c_str(), &st) != 0) {
        return false;
    }
    return (st.st_mode & S_IXUSR) == 0;
}

TEST_CASE("execute reports a non-executable PATH candidate as 126") {
    TempDir dir;
    if (!executable_bit_is_honoured(dir.path)) {
        SKIP("filesystem does not honour the executable bit");
    }
    const std::string program = dir.path + "/aos-non-executable-command";
    write_file(program, "#!/bin/sh\nexit 0\n");
    REQUIRE(chmod(program.c_str(), 0600) == 0);

    aos::inst_t inst = command({"aos-non-executable-command"});
    inst.env["PATH"] = dir.path;
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.status == 126);
}

TEST_CASE("execute continues PATH search after a non-executable candidate") {
    TempDir blocked;
    TempDir usable;
    if (!executable_bit_is_honoured(blocked.path)) {
        SKIP("filesystem does not honour the executable bit");
    }
    const std::string name = "aos-path-eacces-then-success";
    const std::string blocked_program = blocked.path + '/' + name;
    const std::string usable_program = usable.path + '/' + name;
    write_file(blocked_program, "#!/bin/sh\nexit 1\n");
    write_file(usable_program, "#!/bin/sh\nprintf path-continued");
    REQUIRE(chmod(blocked_program.c_str(), 0600) == 0);
    REQUIRE(chmod(usable_program.c_str(), 0700) == 0);

    aos::inst_t inst = command({"aos-path-eacces-then-success"});
    inst.env["PATH"] = blocked.path + ':' + usable.path;
    inst.stdout_path = usable.path + "/stdout";
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.status == 0);
    CHECK(read_file(inst.stdout_path) == "path-continued");
}

TEST_CASE("execute resolves a relative path after changing cwd") {
    TempDir dir;
    const std::string program = dir.path + "/foo";
    write_file(program, "#!/bin/sh\nprintf relative-cwd");
    REQUIRE(chmod(program.c_str(), 0700) == 0);

    aos::inst_t inst = command({"./foo"});
    inst.cwd = dir.path;
    inst.stdout_path = dir.path + "/stdout";
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.status == 0);
    CHECK(read_file(inst.stdout_path) == "relative-cwd");
}

TEST_CASE("execute treats an empty PATH field as the command cwd") {
    TempDir dir;
    const std::string program = dir.path + "/aos-current-dir-command";
    write_file(program, "#!/bin/sh\nprintf empty-path-field");
    REQUIRE(chmod(program.c_str(), 0700) == 0);

    aos::inst_t inst = command({"aos-current-dir-command"});
    inst.cwd = dir.path;
    inst.env["PATH"] = "/aos-cpp-path-entry-that-does-not-exist:";
    inst.stdout_path = dir.path + "/stdout";
    aos::ExecResult result;

    REQUIRE(aos::execute(inst, result) == aos::ExecState::Ok);
    CHECK(result.status == 0);
    CHECK(read_file(inst.stdout_path) == "empty-path-field");
}
