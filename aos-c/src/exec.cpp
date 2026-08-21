/* 要在任何標頭之前宣告，-std=c++11 會定義 __STRICT_ANSI__ 而藏起 POSIX 介面。 */
#define _POSIX_C_SOURCE 200809L

#include "aos/exec.hpp"

#include <cerrno>
#include <fstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aos {
namespace {

/*
 * 把結束狀態寫進 exit_path。父行程在 waitpid 之後才呼叫，因此這裡的失敗
 * 是「子行程跑完了但狀態記不下來」，跟子行程本身的成敗無關。
 */
bool write_exit_status(const std::string &path, int status)
{
    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);

    if (!out) {
        return false;
    }
    out << status << '\n';
    out.close();
    return !out.fail();
}

/*
 * 子行程在 exec 之前失敗時用的兩個結束碼，沿用 shell 的慣例：126 是「指令
 * 找到了但跑不起來」，127 是「根本沒有這個指令」。
 *
 * 這兩個碼就是回報管道的替代品。分辨「execvp 失敗」和「子程式自己回傳
 * 127」需要一條管道，而 shell 從來不分 —— 既然 exit_path 的消費端看的是
 * 「這筆做完了、碼是多少」，那就跟 shell 一樣，把這件事留給 stdout/stderr。
 */
constexpr int kExitSetupFailed = 126;
constexpr int kExitExecFailed = 127;

/* 開啟一個重導向目標並接到 target_fd 上；空路徑代表原樣繼承。 */
bool child_redirect(const std::string &path, int target_fd, int flags)
{
    if (path.empty()) {
        return true;
    }

    const int fd = open(path.c_str(), flags, 0666);

    if (fd < 0) {
        return false;
    }
    if (dup2(fd, target_fd) < 0) {
        close(fd);
        return false;
    }
    if (fd != target_fd) {
        close(fd);
    }
    return true;
}

/*
 * 子行程的全部工作：重導向、切目錄、擴充環境、exec。此函式不會返回。
 *
 * 失敗時用 _exit() 而不是 exit()，後者會跑掉父行程登記的 atexit 處理常式。
 */
[[noreturn]] void run_child(inst_t &inst, std::vector<char *> &argv)
{
    if (!child_redirect(inst.stdin_path, STDIN_FILENO, O_RDONLY) ||
        !child_redirect(inst.stdout_path, STDOUT_FILENO,
                        O_WRONLY | O_CREAT | O_TRUNC) ||
        !child_redirect(inst.stderr_path, STDERR_FILENO,
                        O_WRONLY | O_CREAT | O_TRUNC)) {
        _exit(kExitSetupFailed);
    }
    if (!inst.cwd.empty() && chdir(inst.cwd.c_str()) != 0) {
        _exit(kExitSetupFailed);
    }

    /* env 非空就在繼承的環境上逐筆覆寫/新增(setenv 語意)，不是整組替換。
     * entry 在讀寫時已驗證為合法 KEY=VALUE，所以 '=' 必然存在且不在最前面。
     * 同名的後者覆寫前者，因為是照順序逐筆 setenv。 */
    for (const std::string &entry : inst.env) {
        const std::string::size_type eq = entry.find('=');
        if (setenv(entry.substr(0, eq).c_str(), entry.c_str() + eq + 1, 1) !=
            0) {
            _exit(kExitSetupFailed);   /* 通常是 ENOMEM，歸類為佈置失敗(126) */
        }
    }

    execvp(argv[0], argv.data());
    /* execvp 只有失敗才會返回。 */
    _exit(kExitExecFailed);
}

}  /* namespace */

ExecState execute(inst_t &inst, ExecResult &result)
{
    result = ExecResult();

    if (inst.argv.empty() || inst.argv[0].empty()) {
        return ExecState::InvalidArgument;
    }

    std::vector<char *> argv = to_c_argv(inst);

    const pid_t pid = fork();

    if (pid < 0) {
        /*
         * 這是唯一一種「沒有子行程可以變成結束碼」的失敗，所以也是唯一還
         * 需要在這裡回報的啟動錯誤。
         */
        result.error = errno;
        return ExecState::SpawnFailed;
    }
    if (pid == 0) {
        run_child(inst, argv);
    }

    /* 子行程一定要回收，否則會留下殭屍行程。 */
    int raw_status = 0;
    pid_t waited;

    do {
        waited = waitpid(pid, &raw_status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
        result.error = errno;
        return ExecState::WaitFailed;
    }

    if (WIFSIGNALED(raw_status)) {
        result.status = 128 + WTERMSIG(raw_status);
        result.signalled = true;
    } else if (WIFEXITED(raw_status)) {
        result.status = WEXITSTATUS(raw_status);
    } else {
        return ExecState::WaitFailed;
    }

    /*
     * exit_path 有設就一律寫一個碼，成敗都寫。它是「這筆做完了」的信號，
     * 不是「做對了」的信號 —— 等著讀檔的消費端要能靠它知道這筆結束了，而
     * 126/127 正是為了讓「跑不起來」也有一個碼可寫。
     */
    if (!inst.exit_path.empty() &&
        !write_exit_status(inst.exit_path, result.status)) {
        return ExecState::ExitWriteFailed;
    }

    return ExecState::Ok;
}

const char *to_string(ExecState state)
{
    switch (state) {
    case ExecState::Ok:
        return "ok";
    case ExecState::InvalidArgument:
        return "invalid argument";
    case ExecState::SpawnFailed:
        return "could not fork";
    case ExecState::WaitFailed:
        return "could not wait for command";
    case ExecState::ExitWriteFailed:
        return "could not write exit status";
    }
    return "unknown execution result";
}

}  /* namespace aos */
