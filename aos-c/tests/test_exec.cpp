/* 順序要在任何標頭之前，理由與 src/exec.cpp 相同：-std=c++11 會定義
 * __STRICT_ANSI__，藏起 mkdtemp、setenv 這些 POSIX 介面。 */
#define _POSIX_C_SOURCE 200809L

#include "test_common.hpp"

#include "aos/exec.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

/*
 * test_common.hpp 把 run_exec_tests() 宣告在全域範圍（供 test_main.cpp
 * 呼叫），所以這裡引入 aos 而不是把整個檔案包進 namespace aos 裡。
 */
using namespace aos;

namespace {

std::string read_file(const std::string &path)
{
    std::ifstream in(path.c_str());
    std::ostringstream buffer;

    buffer << in.rdbuf();
    return buffer.str();
}

void write_file(const std::string &path, const std::string &content)
{
    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);

    out << content;
}

bool file_exists(const std::string &path)
{
    return access(path.c_str(), F_OK) == 0;
}

/* 建一個獨一無二的暫存目錄，讓每次測試執行都有乾淨的檔案系統空間。 */
std::string make_temp_dir()
{
    std::string tmpl = "/tmp/aos_c_exec_test_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());

    buf.push_back('\0');

    const char *result = mkdtemp(buf.data());

    CHECK(result != nullptr);
    return std::string(buf.data());
}

/* 刪掉目錄底下所有檔案再刪目錄本身；不假設有哪些檔名存在。 */
void remove_temp_dir(const std::string &dir)
{
    DIR *d = opendir(dir.c_str());

    if (d != nullptr) {
        struct dirent *entry;

        while ((entry = readdir(d)) != nullptr) {
            const std::string name = entry->d_name;

            if (name == "." || name == "..") {
                continue;
            }
            std::remove((dir + "/" + name).c_str());
        }
        closedir(d);
    }
    rmdir(dir.c_str());
}

std::size_t test_echo_stdout(const std::string &dir)
{
    inst_t inst = make_inst({ "echo", "hello world" });
    ExecResult result;

    inst.stdout_path = dir + "/echo_out";

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(result.status == 0);
    CHECK(!result.signalled);
    CHECK(read_file(inst.stdout_path) == "hello world\n");
    return 1;
}

std::size_t test_exit_path_written(const std::string &dir)
{
    inst_t inst = make_inst({ "sh", "-c", "exit 3" });
    ExecResult result;

    inst.exit_path = dir + "/exit_status";

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(result.status == 3);
    CHECK(!result.signalled);
    CHECK(read_file(inst.exit_path) == "3\n");
    return 1;
}

std::size_t test_signalled_child(const std::string &dir)
{
    /* 未使用 dir，但保持相同的呼叫慣例，方便日後這裡也要用到檔案。 */
    static_cast<void>(dir);

    inst_t inst = make_inst({ "sh", "-c", "kill -TERM $$" });
    ExecResult result;

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(result.signalled);
    /* 128 加訊號編號，沿用 shell 慣例；SIGTERM 是 15。 */
    CHECK(result.status == 143);
    return 1;
}

std::size_t test_command_not_found(const std::string &dir)
{
    inst_t inst = make_inst({ "aos-c-test-no-such-command-xyz" });
    ExecResult result;
    const std::string exit_path = dir + "/not_found_exit";

    inst.exit_path = exit_path;

    /*
     * 找不到指令不是這個函式庫的失敗，而是一筆跑完並產出 127 的記錄 ——
     * 跟 shell 一樣。exit_path 有設就一定要寫，否則等著讀檔的人永遠不知道
     * 這一筆結束了。
     */
    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(result.status == 127);
    CHECK(!result.signalled);
    CHECK(read_file(exit_path) == "127\n");
    return 1;
}

std::size_t test_genuine_exit_127_looks_the_same(const std::string &dir)
{
    static_cast<void>(dir);

    inst_t inst = make_inst({ "sh", "-c", "exit 127" });
    ExecResult result;

    /*
     * 子行程自己回傳 127，跟上一個案例的「找不到指令」給出一模一樣的結果。
     * 這是刻意的：要分開它們需要另一條從子行程出來的管道，而 shell 也不
     * 分 —— 在意指令做了什麼的人，看的是它的 stdout 與 stderr。
     */
    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(result.status == 127);
    CHECK(!result.signalled);
    return 1;
}

/* 佈置失敗（重導向開不起來、cwd 不存在）在子行程裡是 126。 */
std::size_t test_setup_failure_is_126(const std::string &dir)
{
    std::size_t cases = 0;

    /* stdin 讀不到：檔案不存在，而且不會被建立。 */
    {
        inst_t inst = make_inst({ "echo", "x" });
        ExecResult result;

        inst.stdin_path = dir + "/no-such-input-file";
        inst.stdout_path = dir + "/stdin_fail_out";
        inst.exit_path = dir + "/stdin_fail_exit";

        CHECK(execute(inst, result) == ExecState::Ok);
        CHECK(result.status == 126);
        CHECK(read_file(inst.exit_path) == "126\n");
        /*
         * 重導向是照 stdin、stdout、stderr 的順序做的，所以卡在第一個就代表
         * 後面全都沒發生：輸出檔連建立都沒有，指令本身也從沒跑起來。
         */
        CHECK(!file_exists(inst.stdout_path));
        ++cases;
    }

    /* stdout 寫不進去：目錄不存在，O_CREAT 也救不了。 */
    {
        inst_t inst = make_inst({ "echo", "x" });
        ExecResult result;

        inst.stdout_path = dir + "/no-such-subdirectory/out";

        CHECK(execute(inst, result) == ExecState::Ok);
        CHECK(result.status == 126);
        ++cases;
    }

    /* cwd 切不過去。 */
    {
        inst_t inst = make_inst({ "sh", "-c", "pwd" });
        ExecResult result;

        inst.cwd = dir + "/no-such-subdirectory";

        CHECK(execute(inst, result) == ExecState::Ok);
        CHECK(result.status == 126);
        ++cases;
    }

    return cases;
}

/* exit_path 是「這筆做完了」的信號，所以成敗都要寫進去一個碼。 */
std::size_t test_exit_path_always_written(const std::string &dir)
{
    struct Case {
        const char *name;
        std::vector<std::string> argv;
        int status;
    };
    const std::vector<Case> cases = {
        { "ok", { "true" }, 0 },
        { "nonzero", { "sh", "-c", "exit 3" }, 3 },
        { "signalled", { "sh", "-c", "kill -TERM $$" }, 143 },
        { "not_found", { "aos-c-test-no-such-command-xyz" }, 127 }
    };
    std::size_t count = 0;

    for (const Case &c : cases) {
        inst_t inst = make_inst(c.argv);
        ExecResult result;

        inst.exit_path = dir + "/always_" + c.name;

        CHECK(execute(inst, result) == ExecState::Ok);
        CHECK(result.status == c.status);
        CHECK(read_file(inst.exit_path) ==
              std::to_string(c.status) + "\n");
        ++count;
    }
    return count;
}

std::size_t test_cwd(const std::string &dir)
{
    std::size_t cases = 0;

    {
        inst_t inst = make_inst({ "sh", "-c", "pwd" });
        ExecResult result;

        inst.cwd = dir;
        inst.stdout_path = dir + "/pwd_out";

        CHECK(execute(inst, result) == ExecState::Ok);

        std::string output = read_file(inst.stdout_path);
        if (!output.empty() && output.back() == '\n') {
            output.pop_back();
        }
        CHECK(output == dir);
        ++cases;
    }
    return cases;
}

std::size_t test_stdin_redirection(const std::string &dir)
{
    inst_t inst = make_inst({ "cat" });
    ExecResult result;
    const std::string stdin_path = dir + "/cat_in";

    write_file(stdin_path, "piped content\n");
    inst.stdin_path = stdin_path;
    inst.stdout_path = dir + "/cat_out";

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(read_file(inst.stdout_path) == "piped content\n");
    return 1;
}

std::size_t test_stdout_truncation(const std::string &dir)
{
    inst_t inst = make_inst({ "echo", "hi" });
    ExecResult result;
    const std::string path = dir + "/truncate_out";

    /* 預先塞進比接下來輸出還長的內容，確認舊內容的尾巴不會殘留。 */
    write_file(path, "this line is much longer than the new output");
    inst.stdout_path = path;

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(read_file(path) == "hi\n");
    return 1;
}

std::size_t test_stderr_redirection(const std::string &dir)
{
    inst_t inst = make_inst({ "sh", "-c", "echo oops >&2" });
    ExecResult result;
    const std::string path = dir + "/stderr_out";

    inst.stderr_path = path;

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(read_file(path) == "oops\n");
    return 1;
}

std::size_t test_env_extends_environment(const std::string &dir)
{
    inst_t inst = make_inst(
        { "sh", "-c", "echo $MYVAR:$AOS_C_TEST_PARENT_ONLY" });
    ExecResult result;
    const std::string out_path = dir + "/env_out";

    /* 父行程自己設的變數，用來確認 env 是擴充而不是整組取代。 */
    setenv("AOS_C_TEST_PARENT_ONLY", "leaked", 1);

    inst.env = { "MYVAR=hello" };
    inst.stdout_path = out_path;

    CHECK(execute(inst, result) == ExecState::Ok);
    /* 擴充：新變數在，父變數也還在。 */
    CHECK(read_file(out_path) == "hello:leaked\n");

    unsetenv("AOS_C_TEST_PARENT_ONLY");
    return 1;
}

/* 同名的 env 覆寫繼承而來的值，其餘父環境保留不動。 */
std::size_t test_env_overrides_inherited(const std::string &dir)
{
    inst_t inst = make_inst({ "sh", "-c", "echo $AOS_C_TEST_OVERRIDE" });
    ExecResult result;
    const std::string out_path = dir + "/env_override_out";

    setenv("AOS_C_TEST_OVERRIDE", "old", 1);

    inst.env = { "AOS_C_TEST_OVERRIDE=new" };
    inst.stdout_path = out_path;

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(read_file(out_path) == "new\n");

    unsetenv("AOS_C_TEST_OVERRIDE");
    return 1;
}

std::size_t test_empty_env_inherits_environment(const std::string &dir)
{
    inst_t inst = make_inst({ "sh", "-c", "echo $AOS_C_TEST_PARENT_ONLY" });
    ExecResult result;
    const std::string out_path = dir + "/env_inherit_out";

    setenv("AOS_C_TEST_PARENT_ONLY", "inherited", 1);

    /* env 留空：子行程應該原封不動地拿到父行程的環境。 */
    inst.stdout_path = out_path;

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(read_file(out_path) == "inherited\n");

    unsetenv("AOS_C_TEST_PARENT_ONLY");
    return 1;
}

/*
 * 重複的鍵是確定的「後者贏」：env 是照順序逐筆 setenv overwrite 套用的，
 * 所以 DUP=first 之後 DUP=second，最後一定是 second。這一層不做去重 ——
 * 要去重的話，那是產生端的事。
 */
std::size_t test_env_is_passed_through_verbatim(const std::string &dir)
{
    inst_t inst = make_inst({ "sh", "-c", "echo $DUP" });
    ExecResult result;
    const std::string out_path = dir + "/env_dup_out";

    inst.env = { "DUP=first", "DUP=second" };
    inst.stdout_path = out_path;

    CHECK(execute(inst, result) == ExecState::Ok);
    CHECK(read_file(out_path) == "second\n");
    return 1;
}

std::size_t test_empty_argv_is_invalid_argument()
{
    inst_t inst;
    ExecResult result;

    CHECK(execute(inst, result) == ExecState::InvalidArgument);
    return 1;
}

std::size_t test_to_string_covers_every_state()
{
    const ExecState states[] = {
        ExecState::Ok,
        ExecState::InvalidArgument,
        ExecState::SpawnFailed,
        ExecState::WaitFailed,
        ExecState::ExitWriteFailed
    };
    std::size_t cases = 0;

    for (ExecState state : states) {
        const char *text = to_string(state);

        CHECK(text != nullptr);
        CHECK(std::strlen(text) > 0);
        ++cases;
    }

    const char *unknown = to_string(static_cast<ExecState>(9999));

    CHECK(unknown != nullptr);
    CHECK(std::strlen(unknown) > 0);
    ++cases;

    return cases;
}

}  /* namespace */

std::size_t run_exec_tests()
{
    const std::string dir = make_temp_dir();
    std::size_t count = 0;

    count += test_echo_stdout(dir);
    count += test_exit_path_written(dir);
    count += test_signalled_child(dir);
    count += test_command_not_found(dir);
    count += test_genuine_exit_127_looks_the_same(dir);
    count += test_setup_failure_is_126(dir);
    count += test_exit_path_always_written(dir);
    count += test_cwd(dir);
    count += test_stdin_redirection(dir);
    count += test_stdout_truncation(dir);
    count += test_stderr_redirection(dir);
    count += test_env_extends_environment(dir);
    count += test_env_overrides_inherited(dir);
    count += test_empty_env_inherits_environment(dir);
    count += test_env_is_passed_through_verbatim(dir);
    count += test_empty_argv_is_invalid_argument();
    count += test_to_string_covers_every_state();

    remove_temp_dir(dir);
    return count;
}
