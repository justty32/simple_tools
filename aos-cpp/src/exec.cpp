#define _POSIX_C_SOURCE 200809L

#include <aos/exec.hpp>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aos {
namespace {

constexpr int kExitSetupFailed = 126;
constexpr int kExitExecFailed = 127;
constexpr std::uint64_t kTimeoutGraceMs = 2000;
constexpr std::uint64_t kMaxPollMs = 50;

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

bool child_redirect(const std::string &path, int target_fd, int flags) {
    if (path.empty()) {
        return true;
    }

    const int fd = open_retry(path.c_str(), flags, 0666);
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

[[noreturn]] void run_child(inst_t &inst, std::vector<char *> &argv) {
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

    for (const auto &entry : inst.env) {
        if (setenv(entry.first.c_str(), entry.second.c_str(), 1) != 0) {
            _exit(kExitSetupFailed);
        }
    }

    execvp(argv[0], argv.data());
    _exit(kExitExecFailed);
}

pid_t wait_retry(pid_t pid, int *status, int options) {
    pid_t waited;
    do {
        waited = waitpid(pid, status, options);
    } while (waited < 0 && errno == EINTR);
    return waited;
}

bool monotonic_now(timespec &value) {
    return clock_gettime(CLOCK_MONOTONIC, &value) == 0;
}

std::uint64_t elapsed_ms(const timespec &start, const timespec &end) {
    std::uint64_t seconds = static_cast<std::uint64_t>(end.tv_sec - start.tv_sec);
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
        --seconds;
        nanoseconds += 1000000000L;
    }
    if (seconds > std::numeric_limits<std::uint64_t>::max() / 1000) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return seconds * 1000 + static_cast<std::uint64_t>(nanoseconds / 1000000L);
}

void sleep_ms(std::uint64_t milliseconds) {
    timespec request{
        static_cast<time_t>(milliseconds / 1000),
        static_cast<long>((milliseconds % 1000) * 1000000),
    };
    timespec remaining{};
    while (nanosleep(&request, &remaining) < 0 && errno == EINTR) {
        request = remaining;
    }
}

bool wait_until(pid_t pid, std::uint64_t limit_ms, int &raw_status,
                int &error) {
    timespec start{};
    if (!monotonic_now(start)) {
        error = errno;
        return false;
    }

    std::uint64_t poll_ms = 1;
    for (;;) {
        const pid_t waited = wait_retry(pid, &raw_status, WNOHANG);
        if (waited == pid) {
            return true;
        }
        if (waited < 0) {
            error = errno;
            return false;
        }

        timespec now{};
        if (!monotonic_now(now)) {
            error = errno;
            return false;
        }
        const std::uint64_t elapsed = elapsed_ms(start, now);
        if (elapsed >= limit_ms) {
            return false;
        }
        sleep_ms(poll_ms < limit_ms - elapsed ? poll_ms : limit_ms - elapsed);
        poll_ms = poll_ms < kMaxPollMs / 2 ? poll_ms * 2 : kMaxPollMs;
    }
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

    const pid_t pid = fork();
    if (pid < 0) {
        result.error = errno;
        return ExecState::SpawnFailed;
    }
    if (pid == 0) {
        if (setpgid(0, 0) != 0) {
            _exit(kExitSetupFailed);
        }
        run_child(inst, argv);
    }

    if (setpgid(pid, pid) != 0 && errno != EACCES) {
        const int saved_error = errno;
        kill(pid, SIGKILL);
        int ignored_status = 0;
        wait_retry(pid, &ignored_status, 0);
        result.error = saved_error;
        return ExecState::SpawnFailed;
    }

    int raw_status = 0;
    pid_t waited = 0;
    if (inst.timeout_ms == 0) {
        waited = wait_retry(pid, &raw_status, 0);
    } else {
        int wait_error = 0;
        if (wait_until(pid, inst.timeout_ms, raw_status, wait_error)) {
            waited = pid;
        } else if (wait_error != 0) {
            result.error = wait_error;
            return ExecState::WaitFailed;
        } else {
            result.timed_out = true;
            kill(-pid, SIGTERM);
            if (wait_until(pid, kTimeoutGraceMs, raw_status, wait_error)) {
                waited = pid;
            } else if (wait_error != 0) {
                result.error = wait_error;
                return ExecState::WaitFailed;
            } else {
                kill(-pid, SIGKILL);
                waited = wait_retry(pid, &raw_status, 0);
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
