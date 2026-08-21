#define _POSIX_C_SOURCE 200809L

#include <aos/exec.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string read_file(const std::string &path) {
    std::ifstream input(path);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

void write_file(const std::string &path, const std::string &content) {
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    output << content;
}

std::string make_temp_dir() {
    std::string pattern = "aos_exec_test_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    REQUIRE(mkdtemp(buffer.data()) != nullptr);
    return buffer.data();
}

void remove_temp_dir(const std::string &path) {
    DIR *dir = opendir(path.c_str());
    if (dir != nullptr) {
        while (dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name != "." && name != "..") {
                std::remove((path + "/" + name).c_str());
            }
        }
        closedir(dir);
    }
    rmdir(path.c_str());
}

struct TempDir {
    TempDir() : path(make_temp_dir()) {}
    ~TempDir() { remove_temp_dir(path); }
    std::string path;
};

aos::inst_t command(std::initializer_list<const char *> args) {
    aos::inst_t inst;
    for (const char *arg : args) {
        inst.argv.emplace_back(arg);
    }
    return inst;
}

}  // namespace

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
