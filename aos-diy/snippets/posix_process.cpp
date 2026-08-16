#include "posix_process.hpp"

#include "unique_fd.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace aos_diy::snippets {
namespace {

enum class ChildStage : int {
  change_directory,
  redirect_stdin,
  redirect_stdout,
  redirect_stderr,
  execute,
};

struct ChildError {
  ChildStage stage;
  int error;
};

const char *stage_name(ChildStage stage) {
  switch (stage) {
  case ChildStage::change_directory:
    return "chdir";
  case ChildStage::redirect_stdin:
    return "dup2(stdin)";
  case ChildStage::redirect_stdout:
    return "dup2(stdout)";
  case ChildStage::redirect_stderr:
    return "dup2(stderr)";
  case ChildStage::execute:
    return "execve";
  }
  return "child setup";
}

Outcome launch_error(int error, const std::string &operation) {
  return Outcome::launch_error(
      error, operation + ": " + std::string{std::strerror(error)});
}

std::vector<char *> as_c_strings(std::vector<std::string> &items) {
  std::vector<char *> result;
  result.reserve(items.size() + 1);
  for (std::string &item : items) {
    result.push_back(item.data());
  }
  result.push_back(nullptr);
  return result;
}

std::vector<std::string> clean_environment() {
  return {"PATH=/usr/local/bin:/usr/bin:/bin"};
}

struct LoadedEnvironment {
  bool ok = false;
  std::string reason;
  std::vector<std::string> entries;
};

LoadedEnvironment source_environment(const std::string &path,
                                     const UniqueFd &stderr_fd) {
  static constexpr const char *script =
      ". \"$1\" >&2 || exit 1\n"
      "env -0\n"
      "printf '%s\\0' \"$2\"\n";
  static constexpr const char *sentinel = "__AOS_DIY_ENV_END__";
  LoadedEnvironment result;

  int raw_pipe[2] = {-1, -1};
  if (::pipe2(raw_pipe, O_CLOEXEC) < 0) {
    result.reason = "source env pipe: " + std::string{std::strerror(errno)};
    return result;
  }
  UniqueFd output_read{raw_pipe[0]};
  UniqueFd output_write{raw_pipe[1]};
  UniqueFd devnull{::open("/dev/null", O_RDONLY | O_CLOEXEC)};
  if (!devnull.valid()) {
    result.reason = "source env /dev/null: " +
                    std::string{std::strerror(errno)};
    return result;
  }

  std::vector<std::string> arguments = {"sh", "-c", script, "sh", path,
                                         sentinel};
  std::vector<std::string> base = clean_environment();
  std::vector<char *> argv = as_c_strings(arguments);
  std::vector<char *> environment = as_c_strings(base);

  std::fflush(nullptr);
  const pid_t child = ::fork();
  if (child < 0) {
    result.reason = "source env fork: " + std::string{std::strerror(errno)};
    return result;
  }
  if (child == 0) {
    output_read.reset();
    if (::dup2(devnull.get(), STDIN_FILENO) >= 0 &&
        ::dup2(output_write.get(), STDOUT_FILENO) >= 0 &&
        (!stderr_fd.valid() ||
         ::dup2(stderr_fd.get(), STDERR_FILENO) >= 0)) {
      ::execve("/bin/sh", argv.data(), environment.data());
    }
    ::_exit(127);
  }

  output_write.reset();
  std::string dump;
  char buffer[8192];
  for (;;) {
    const ssize_t count = ::read(output_read.get(), buffer, sizeof(buffer));
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      result.reason = "source env read: " +
                      std::string{std::strerror(errno)};
      break;
    }
    if (count == 0) {
      break;
    }
    dump.append(buffer, static_cast<std::size_t>(count));
  }

  int wait_status = 0;
  pid_t waited;
  do {
    waited = ::waitpid(child, &wait_status, 0);
  } while (waited < 0 && errno == EINTR);
  if (!result.reason.empty()) {
    return result;
  }
  if (waited < 0 || !WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0) {
    result.reason = "could not source env file: " + path;
    return result;
  }

  std::size_t start = 0;
  while (start < dump.size()) {
    const std::size_t stop = dump.find('\0', start);
    if (stop == std::string::npos) {
      break;
    }
    result.entries.emplace_back(dump, start, stop - start);
    start = stop + 1;
  }
  if (result.entries.empty() || result.entries.back() != sentinel) {
    result.entries.clear();
    result.reason = "incomplete environment dump from: " + path;
    return result;
  }
  result.entries.pop_back();
  result.ok = true;
  return result;
}

UniqueFd open_input(const std::optional<std::string> &path) {
  return path ? UniqueFd{::open(path->c_str(), O_RDONLY | O_CLOEXEC)}
              : UniqueFd{};
}

UniqueFd open_output(const std::optional<std::string> &path) {
  return path ? UniqueFd{::open(path->c_str(),
                                O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600)}
              : UniqueFd{};
}

[[noreturn]] void report_child_error(int fd, ChildStage stage,
                                     int error) noexcept {
  const ChildError payload{stage, error};
  const char *cursor = reinterpret_cast<const char *>(&payload);
  std::size_t remaining = sizeof(payload);

  while (remaining != 0) {
    const ssize_t count = ::write(fd, cursor, remaining);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    cursor += count;
    remaining -= static_cast<std::size_t>(count);
  }
  ::_exit(127);
}

void redirect_or_report(const UniqueFd &source, int destination,
                        ChildStage stage, int report_fd) noexcept {
  if (source.valid() && ::dup2(source.get(), destination) < 0) {
    report_child_error(report_fd, stage, errno);
  }
}

bool read_child_error(int fd, ChildError &payload, int &read_error) {
  char *cursor = reinterpret_cast<char *>(&payload);
  std::size_t received = 0;

  while (received < sizeof(payload)) {
    const ssize_t count = ::read(fd, cursor + received, sizeof(payload) - received);
    if (count == 0) {
      return received == sizeof(payload);
    }
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      read_error = errno;
      return false;
    }
    received += static_cast<std::size_t>(count);
  }
  return true;
}

} // namespace

Outcome run_process(const Call &call) {
  if (call.argv.empty() || call.argv.front().empty()) {
    return Outcome::rejected("argv and argv[0] must not be empty");
  }

  UniqueFd input = open_input(call.stdin_path);
  if (call.stdin_path && !input.valid()) {
    return launch_error(errno, "open(stdin)");
  }
  UniqueFd output = open_output(call.stdout_path);
  if (call.stdout_path && !output.valid()) {
    return launch_error(errno, "open(stdout)");
  }
  UniqueFd error_output = open_output(call.stderr_path);
  if (call.stderr_path && !error_output.valid()) {
    return launch_error(errno, "open(stderr)");
  }

  std::vector<std::string> environment_entries = clean_environment();
  if (call.env) {
    LoadedEnvironment loaded = source_environment(*call.env, error_output);
    if (!loaded.ok) {
      return Outcome::launch_error(0, loaded.reason);
    }
    environment_entries = std::move(loaded.entries);
  }
  std::vector<char *> argv;
  argv.reserve(call.argv.size() + 1);
  for (const std::string &argument : call.argv) {
    argv.push_back(const_cast<char *>(argument.c_str()));
  }
  argv.push_back(nullptr);
  std::vector<char *> environment = as_c_strings(environment_entries);

  int report_pipe[2] = {-1, -1};
  if (::pipe2(report_pipe, O_CLOEXEC) < 0) {
    return launch_error(errno, "pipe2");
  }
  UniqueFd report_read{report_pipe[0]};
  UniqueFd report_write{report_pipe[1]};

  // Avoid duplicating buffered stdio output into the child at fork time.
  std::fflush(nullptr);
  const pid_t child = ::fork();
  if (child < 0) {
    return launch_error(errno, "fork");
  }

  if (child == 0) {
    report_read.reset();

    if (call.cwd && ::chdir(call.cwd->c_str()) < 0) {
      report_child_error(report_write.get(), ChildStage::change_directory, errno);
    }
    redirect_or_report(input, STDIN_FILENO, ChildStage::redirect_stdin,
                       report_write.get());
    redirect_or_report(output, STDOUT_FILENO, ChildStage::redirect_stdout,
                       report_write.get());
    redirect_or_report(error_output, STDERR_FILENO, ChildStage::redirect_stderr,
                       report_write.get());

    ::execve(call.argv.front().c_str(), argv.data(), environment.data());
    report_child_error(report_write.get(), ChildStage::execute, errno);
  }

  report_write.reset();
  ChildError child_error{};
  int pipe_read_error = 0;
  const bool child_reported =
      read_child_error(report_read.get(), child_error, pipe_read_error);
  report_read.reset();

  int wait_status = 0;
  pid_t waited = -1;
  do {
    waited = ::waitpid(child, &wait_status, 0);
  } while (waited < 0 && errno == EINTR);

  if (waited < 0) {
    return Outcome::unknown("waitpid: " + std::string{std::strerror(errno)});
  }
  if (pipe_read_error != 0) {
    return Outcome::unknown("read(exec report): " +
                            std::string{std::strerror(pipe_read_error)});
  }
  if (child_reported) {
    return launch_error(child_error.error, stage_name(child_error.stage));
  }
  if (WIFEXITED(wait_status)) {
    return Outcome::exited(WEXITSTATUS(wait_status));
  }
  if (WIFSIGNALED(wait_status)) {
    return Outcome::signalled(WTERMSIG(wait_status));
  }
  return Outcome::unknown("child did not exit or terminate by signal");
}

} // namespace aos_diy::snippets
