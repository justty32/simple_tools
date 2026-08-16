#include "posix_process.hpp"

#include "unique_fd.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace aos_diy {
namespace snippets11 {
namespace {

enum class ChildStage {
  change_directory,
  redirect_stdin,
  redirect_stdout,
  redirect_stderr,
  execute
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
      error, operation + ": " + std::string(std::strerror(error)));
}

UniqueFd open_input(const OptionalString &path) {
  return path.has_value()
             ? UniqueFd(::open(path->c_str(), O_RDONLY | O_CLOEXEC))
             : UniqueFd();
}

UniqueFd open_output(const OptionalString &path) {
  return path.has_value()
             ? UniqueFd(::open(path->c_str(),
                               O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600))
             : UniqueFd();
}

int make_report_pipe(int pipe_fds[2]) {
  if (::pipe(pipe_fds) < 0) {
    return errno;
  }
  const int flags = ::fcntl(pipe_fds[1], F_GETFD);
  if (flags < 0 || ::fcntl(pipe_fds[1], F_SETFD, flags | FD_CLOEXEC) < 0) {
    const int error = errno;
    (void)::close(pipe_fds[0]);
    (void)::close(pipe_fds[1]);
    pipe_fds[0] = -1;
    pipe_fds[1] = -1;
    return error;
  }
  return 0;
}

std::vector<char *> as_c_strings(std::vector<std::string> &items) {
  std::vector<char *> result;
  result.reserve(items.size() + 1U);
  for (std::size_t i = 0U; i < items.size(); ++i) {
    result.push_back(const_cast<char *>(items[i].c_str()));
  }
  result.push_back(NULL);
  return result;
}

std::vector<std::string> clean_environment() {
  std::vector<std::string> result;
  result.push_back("PATH=/usr/local/bin:/usr/bin:/bin");
  return result;
}

struct LoadedEnvironment {
  LoadedEnvironment() : ok(false) {}
  bool ok;
  std::string reason;
  std::vector<std::string> entries;
};

LoadedEnvironment source_environment(const std::string &path,
                                     const UniqueFd &stderr_fd) {
  static const char script[] =
      ". \"$1\" >&2 || exit 1\n"
      "env -0\n"
      "printf '%s\\0' \"$2\"\n";
  static const char sentinel[] = "__AOS_DIY_ENV_END__";
  LoadedEnvironment result;
  int raw_pipe[2] = {-1, -1};
  const int pipe_error = make_report_pipe(raw_pipe);
  if (pipe_error != 0) {
    result.reason = "source env pipe: " +
                    std::string(std::strerror(pipe_error));
    return result;
  }
  UniqueFd output_read(raw_pipe[0]);
  UniqueFd output_write(raw_pipe[1]);
  UniqueFd devnull(::open("/dev/null", O_RDONLY | O_CLOEXEC));
  if (!devnull.valid()) {
    result.reason = "source env /dev/null: " +
                    std::string(std::strerror(errno));
    return result;
  }

  std::vector<std::string> arguments;
  arguments.push_back("sh");
  arguments.push_back("-c");
  arguments.push_back(script);
  arguments.push_back("sh");
  arguments.push_back(path);
  arguments.push_back(sentinel);
  std::vector<std::string> base = clean_environment();
  std::vector<char *> argv = as_c_strings(arguments);
  std::vector<char *> environment = as_c_strings(base);

  (void)std::fflush(NULL);
  const pid_t child = ::fork();
  if (child < 0) {
    result.reason = "source env fork: " +
                    std::string(std::strerror(errno));
    return result;
  }
  if (child == 0) {
    output_read.reset();
    if (::dup2(devnull.get(), STDIN_FILENO) >= 0 &&
        ::dup2(output_write.get(), STDOUT_FILENO) >= 0 &&
        (!stderr_fd.valid() ||
         ::dup2(stderr_fd.get(), STDERR_FILENO) >= 0)) {
      ::execve("/bin/sh", &argv[0], &environment[0]);
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
                      std::string(std::strerror(errno));
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

  std::size_t start = 0U;
  while (start < dump.size()) {
    const std::size_t stop = dump.find('\0', start);
    if (stop == std::string::npos) {
      break;
    }
    result.entries.push_back(dump.substr(start, stop - start));
    start = stop + 1U;
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

void report_child_error(int fd, ChildStage stage, int error) {
  const ChildError payload = {stage, error};
  const char *cursor = reinterpret_cast<const char *>(&payload);
  std::size_t remaining = sizeof(payload);
  while (remaining != 0U) {
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
                        ChildStage stage, int report_fd) {
  if (source.valid() && ::dup2(source.get(), destination) < 0) {
    report_child_error(report_fd, stage, errno);
  }
}

// 1: complete report, 0: exec closed the pipe, negative: read error.
int read_child_error(int fd, ChildError &payload) {
  char *cursor = reinterpret_cast<char *>(&payload);
  std::size_t received = 0U;
  while (received < sizeof(payload)) {
    const ssize_t count = ::read(fd, cursor + received, sizeof(payload) - received);
    if (count == 0) {
      return received == 0U ? 0 : -EIO;
    }
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -errno;
    }
    received += static_cast<std::size_t>(count);
  }
  return 1;
}

} // namespace

Outcome run_process(const Call &call) {
  if (call.argv.empty() || call.argv.front().empty()) {
    return Outcome::rejected("argv and argv[0] must not be empty");
  }

  UniqueFd input = open_input(call.stdin_path);
  if (call.stdin_path.has_value() && !input.valid()) {
    return launch_error(errno, "open(stdin)");
  }
  UniqueFd output = open_output(call.stdout_path);
  if (call.stdout_path.has_value() && !output.valid()) {
    return launch_error(errno, "open(stdout)");
  }
  UniqueFd error_output = open_output(call.stderr_path);
  if (call.stderr_path.has_value() && !error_output.valid()) {
    return launch_error(errno, "open(stderr)");
  }

  std::vector<char *> argv;
  for (std::size_t i = 0U; i < call.argv.size(); ++i) {
    argv.push_back(const_cast<char *>(call.argv[i].c_str()));
  }
  argv.push_back(NULL);

  std::vector<std::string> environment_entries = clean_environment();
  if (call.env.has_value()) {
    LoadedEnvironment loaded = source_environment(*call.env, error_output);
    if (!loaded.ok) {
      return Outcome::launch_error(0, loaded.reason);
    }
    environment_entries.swap(loaded.entries);
  }
  std::vector<char *> environment = as_c_strings(environment_entries);

  int raw_pipe[2] = {-1, -1};
  const int pipe_error = make_report_pipe(raw_pipe);
  if (pipe_error != 0) {
    return launch_error(pipe_error, "pipe/fcntl");
  }
  UniqueFd report_read(raw_pipe[0]);
  UniqueFd report_write(raw_pipe[1]);

  (void)std::fflush(NULL);
  const pid_t child = ::fork();
  if (child < 0) {
    return launch_error(errno, "fork");
  }
  if (child == 0) {
    report_read.reset();
    if (call.cwd.has_value() && ::chdir(call.cwd->c_str()) < 0) {
      report_child_error(report_write.get(), ChildStage::change_directory,
                         errno);
    }
    redirect_or_report(input, STDIN_FILENO, ChildStage::redirect_stdin,
                       report_write.get());
    redirect_or_report(output, STDOUT_FILENO, ChildStage::redirect_stdout,
                       report_write.get());
    redirect_or_report(error_output, STDERR_FILENO,
                       ChildStage::redirect_stderr, report_write.get());
    ::execve(call.argv.front().c_str(), &argv[0], &environment[0]);
    report_child_error(report_write.get(), ChildStage::execute, errno);
  }

  report_write.reset();
  ChildError child_error;
  const int report_result = read_child_error(report_read.get(), child_error);
  report_read.reset();

  int wait_status = 0;
  pid_t waited;
  do {
    waited = ::waitpid(child, &wait_status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited < 0) {
    return Outcome::unknown("waitpid: " + std::string(std::strerror(errno)));
  }
  if (report_result < 0) {
    return Outcome::unknown("could not read exec report pipe");
  }
  if (report_result == 1) {
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

} // namespace snippets11
} // namespace aos_diy
