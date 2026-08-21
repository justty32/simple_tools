#define _POSIX_C_SOURCE 200809L

#include <aos/exec.hpp>

#include "spawn_prep.hpp"
#include "wait.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aos {
namespace {

constexpr int kExitSetupFailed = 126;
constexpr int kExitExecFailed = 127;
constexpr std::uint64_t kTimeoutGraceMs = 2000;

int open_retry(const char *path, int flags, mode_t mode = 0) {
    int fd;
    do {
        fd = open(path, flags, mode);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

bool write_all(int fd, const char *data, std::size_t size) {
    while (size != 0) {
        ssize_t written;
        do {
            written = write(fd, data, size);
        } while (written < 0 && errno == EINTR);

        if (written <= 0) {
            return false;
        }
        data += written;
        size -= static_cast<std::size_t>(written);
    }
    return true;
}

bool write_exit_status(const std::string &path, int status) {
    char buffer[32];
    const int length = std::snprintf(buffer, sizeof(buffer), "%d\n", status);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(buffer)) {
        return false;
    }

    const int fd = open_retry(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return false;
    }

    const bool wrote = write_all(fd, buffer, static_cast<std::size_t>(length));
    const bool closed = close(fd) == 0;
    return wrote && closed;
}

bool child_redirect(const char *path, int target_fd, int flags) {
    if (path == nullptr) {
        return true;
    }

    const int fd = open_retry(path, flags, 0666);
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

struct ChildPlan {
    const char *stdin_path;
    const char *stdout_path;
    const char *stderr_path;
    const char *cwd;
    const char *executable;
    char *const *argv;
    char *const *envp;
    int failure_status;
};

[[noreturn]] void run_child(const ChildPlan &plan) {
    if (!child_redirect(plan.stdin_path, STDIN_FILENO, O_RDONLY) ||
        !child_redirect(plan.stdout_path, STDOUT_FILENO,
                        O_WRONLY | O_CREAT | O_TRUNC) ||
        !child_redirect(plan.stderr_path, STDERR_FILENO,
                        O_WRONLY | O_CREAT | O_TRUNC)) {
        _exit(kExitSetupFailed);
    }
    if (plan.cwd != nullptr && chdir(plan.cwd) != 0) {
        _exit(kExitSetupFailed);
    }
    if (plan.failure_status != 0) {
        _exit(plan.failure_status);
    }

    execve(plan.executable, plan.argv, plan.envp);
    _exit(kExitExecFailed);
}

}  // namespace

ExecState execute(inst_t &inst, ExecResult &result) {
    result = ExecResult{};

    if (inst.argv.empty() || inst.argv[0].empty()) {
        return ExecState::InvalidArgument;
    }

    std::vector<char *> argv;
    argv.reserve(inst.argv.size() + 1);
    for (std::string &arg : inst.argv) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    detail::SpawnPrep prep;
    detail::prepare_spawn(inst, prep);
    const auto path_or_null = [](const std::string &path) {
        return path.empty() ? nullptr : path.c_str();
    };
    const ChildPlan child_plan{
        path_or_null(inst.stdin_path), path_or_null(inst.stdout_path),
        path_or_null(inst.stderr_path), path_or_null(inst.cwd),
        prep.executable.c_str(), argv.data(), prep.envp.data(),
        prep.failure_status,
    };

    const pid_t pid = fork();
    if (pid < 0) {
        result.error = errno;
        return ExecState::SpawnFailed;
    }
    if (pid == 0) {
        if (setpgid(0, 0) != 0) {
            _exit(kExitSetupFailed);
        }
        run_child(child_plan);
    }

    if (setpgid(pid, pid) != 0 && errno != EACCES) {
        const int saved_error = errno;
        kill(pid, SIGKILL);
        int ignored_status = 0;
        detail::wait_retry(pid, &ignored_status, 0);
        result.error = saved_error;
        return ExecState::SpawnFailed;
    }

    int raw_status = 0;
    pid_t waited = 0;
    if (inst.timeout_ms == 0) {
        waited = detail::wait_retry(pid, &raw_status, 0);
    } else {
        int wait_error = 0;
        if (detail::wait_until(pid, inst.timeout_ms, raw_status, wait_error)) {
            waited = pid;
        } else if (wait_error != 0) {
            result.error = wait_error;
            return ExecState::WaitFailed;
        } else {
            result.timed_out = true;
            kill(-pid, SIGTERM);
            if (detail::wait_until(pid, kTimeoutGraceMs, raw_status, wait_error)) {
                waited = pid;
                /*
                 * 領頭的子行程死了，不代表整個群組死了：忽略 SIGTERM 的孫行程
                 * 會活下來，而殺得掉孫行程正是這裡用行程群組的全部理由。收掉
                 * 領頭者之後補一發 SIGKILL 掃過剩下的成員 —— SIGKILL 擋不掉。
                 * 群組已經空了的話 kill 會回 ESRCH，那正是我們要的「無事發生」。
                 */
                kill(-pid, SIGKILL);
            } else if (wait_error != 0) {
                result.error = wait_error;
                return ExecState::WaitFailed;
            } else {
                kill(-pid, SIGKILL);
                waited = detail::wait_retry(pid, &raw_status, 0);
            }
        }
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

const char *to_string(ExecState state) noexcept {
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

}  // namespace aos
