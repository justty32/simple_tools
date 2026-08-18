/* 要在任何標頭之前宣告，-std=c++11 會定義 __STRICT_ANSI__ 而藏起 POSIX 介面。 */
#define _POSIX_C_SOURCE 200809L

#include "aos/exec.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#define AOS_EXEC_POSIX 1
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#define AOS_EXEC_POSIX 0
#endif

namespace aos {
namespace {

#if AOS_EXEC_POSIX

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

/* 子行程在 exec 之前可能失敗的每個步驟，用來把 errno 對應回 ExecState。 */
enum class Stage : unsigned char {
    Stdin,
    Stdout,
    Stderr,
    Chdir,
    Exec
};

/* 子行程透過 CLOEXEC 管道回報失敗；exec 成功時管道會自動關閉。 */
struct ChildError {
    Stage stage;
    int error;
};

ExecState stage_to_state(Stage stage)
{
    switch (stage) {
    case Stage::Stdin:
        return ExecState::OpenStdinFailed;
    case Stage::Stdout:
        return ExecState::OpenStdoutFailed;
    case Stage::Stderr:
        return ExecState::OpenStderrFailed;
    case Stage::Chdir:
        return ExecState::ChdirFailed;
    case Stage::Exec:
        break;
    }
    return ExecState::SpawnFailed;
}

/*
 * 只在子行程中呼叫：回報失敗的步驟與 errno，然後立刻結束。
 * 這裡不能用 exit()，那會跑掉父行程登記的 atexit 處理常式。
 */
[[noreturn]] void child_fail(int pipe_fd, Stage stage, int error)
{
    const ChildError message = { stage, error };
    /* 寫不出去也無計可施，父行程會退回一般的 SpawnFailed。 */
    ssize_t ignored = write(pipe_fd, &message, sizeof(message));

    static_cast<void>(ignored);
    _exit(127);
}

/* 開啟一個重導向目標並接到 target_fd 上；空路徑代表原樣繼承。 */
void child_redirect(int pipe_fd, const std::string &path, int target_fd,
                    int flags, Stage stage)
{
    if (path.empty()) {
        return;
    }

    const int fd = open(path.c_str(), flags, 0666);

    if (fd < 0) {
        child_fail(pipe_fd, stage, errno);
    }
    if (dup2(fd, target_fd) < 0) {
        child_fail(pipe_fd, stage, errno);
    }
    if (fd != target_fd) {
        close(fd);
    }
}

/* 子行程的全部工作：重導向、切目錄、換環境、exec。此函式不會返回。 */
[[noreturn]] void run_child(int pipe_fd, inst_t &inst,
                            std::vector<char *> &argv,
                            std::vector<char *> &envp, bool replace_env)
{
    child_redirect(pipe_fd, inst.stdin_path, STDIN_FILENO, O_RDONLY,
                   Stage::Stdin);
    child_redirect(pipe_fd, inst.stdout_path, STDOUT_FILENO,
                   O_WRONLY | O_CREAT | O_TRUNC, Stage::Stdout);
    child_redirect(pipe_fd, inst.stderr_path, STDERR_FILENO,
                   O_WRONLY | O_CREAT | O_TRUNC, Stage::Stderr);

    if (!inst.cwd.empty() && chdir(inst.cwd.c_str()) != 0) {
        child_fail(pipe_fd, Stage::Chdir, errno);
    }
    if (replace_env) {
        /* execvpe 是 glibc 專有的，改設 environ 才是可攜的做法。 */
        environ = envp.data();
    }

    execvp(argv[0], argv.data());
    child_fail(pipe_fd, Stage::Exec, errno);
}

#endif /* AOS_EXEC_POSIX */

}  /* namespace */

#if AOS_EXEC_POSIX

ExecState execute(inst_t &inst, ExecResult &result)
{
    result = ExecResult();

    if (inst.argv.empty() || inst.argv[0].empty()) {
        return ExecState::InvalidArgument;
    }

    /*
     * env 是空的就代表繼承，不是「換成一個空的環境」；兩者在 execvp 之前
     * 只差在要不要動 environ。
     */
    const bool replace_env = !inst.env.empty();
    std::vector<char *> envp = to_c_envp(inst);
    std::vector<char *> argv = to_c_argv(inst);

    /*
     * 這個管道是唯一能把「指令不存在」和「指令跑了並回傳 127」分開的辦法。
     * 子行程只在失敗時寫入；exec 成功時 CLOEXEC 會把它關掉，父行程於是讀
     * 到 0 個位元組。
     */
    int pipe_fds[2];

    if (pipe(pipe_fds) != 0) {
        result.error = errno;
        return ExecState::SpawnFailed;
    }
    if (fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC) != 0) {
        result.error = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return ExecState::SpawnFailed;
    }

    const pid_t pid = fork();

    if (pid < 0) {
        result.error = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return ExecState::SpawnFailed;
    }
    if (pid == 0) {
        close(pipe_fds[0]);
        run_child(pipe_fds[1], inst, argv, envp, replace_env);
    }

    close(pipe_fds[1]);

    ChildError failure = { Stage::Exec, 0 };
    const ssize_t got = read(pipe_fds[0], &failure, sizeof(failure));

    close(pipe_fds[0]);

    /* 無論子行程有沒有起來，都必須回收它，否則會留下殭屍行程。 */
    int raw_status = 0;
    pid_t waited;

    do {
        waited = waitpid(pid, &raw_status, 0);
    } while (waited < 0 && errno == EINTR);

    if (got == static_cast<ssize_t>(sizeof(failure))) {
        result.error = failure.error;
        return stage_to_state(failure.stage);
    }
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

    if (!inst.exit_path.empty() &&
        !write_exit_status(inst.exit_path, result.status)) {
        return ExecState::ExitWriteFailed;
    }

    return ExecState::Ok;
}

#else /* !AOS_EXEC_POSIX */

ExecState execute(inst_t &inst, ExecResult &result)
{
    /*
     * Windows 需要 CreateProcess 搭配 STARTUPINFO 的控制代碼重導向，而且
     * 沒有 fork，所以它是另一個實作而不是這一個的變體。在補上之前，這裡
     * 明說不支援，而不是給出會誤導的成功。
     */
    static_cast<void>(inst);
    result = ExecResult();
    return ExecState::PlatformUnsupported;
}

#endif /* AOS_EXEC_POSIX */

const char *to_string(ExecState state)
{
    switch (state) {
    case ExecState::Ok:
        return "ok";
    case ExecState::InvalidArgument:
        return "invalid argument";
    case ExecState::OpenStdinFailed:
        return "could not open stdin redirection";
    case ExecState::OpenStdoutFailed:
        return "could not open stdout redirection";
    case ExecState::OpenStderrFailed:
        return "could not open stderr redirection";
    case ExecState::ChdirFailed:
        return "could not change working directory";
    case ExecState::SpawnFailed:
        return "could not start command";
    case ExecState::WaitFailed:
        return "could not wait for command";
    case ExecState::ExitWriteFailed:
        return "could not write exit status";
    case ExecState::PlatformUnsupported:
        return "process spawning is not implemented on this platform";
    }
    return "unknown execution result";
}

}  /* namespace aos */
