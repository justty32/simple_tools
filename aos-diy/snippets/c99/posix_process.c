#include "posix_process.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum ChildStage {
  CHILD_CHDIR,
  CHILD_STDIN,
  CHILD_STDOUT,
  CHILD_STDERR,
  CHILD_EXEC
} ChildStage;

typedef struct ChildError {
  int stage;
  int error;
} ChildError;

static const char *stage_name(int stage) {
  switch ((ChildStage)stage) {
  case CHILD_CHDIR:
    return "chdir";
  case CHILD_STDIN:
    return "dup2(stdin)";
  case CHILD_STDOUT:
    return "dup2(stdout)";
  case CHILD_STDERR:
    return "dup2(stderr)";
  case CHILD_EXEC:
    return "execve";
  }
  return "child setup";
}

static AosOutcome launch_error(int error, const char *operation) {
  char reason[256];
  (void)snprintf(reason, sizeof(reason), "%s: %s", operation, strerror(error));
  return aos_outcome_launch_error(error, reason);
}

static int open_optional_input(const char *path) {
  return path == NULL ? -1 : open(path, O_RDONLY | O_CLOEXEC);
}

static int open_optional_output(const char *path) {
  return path == NULL
             ? -1
             : open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
}

static void close_if_open(int fd) {
  if (fd >= 0) {
    (void)close(fd);
  }
}

static int set_close_on_exec(int fd) {
  int flags = fcntl(fd, F_GETFD);
  if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
    return errno;
  }
  return 0;
}

typedef struct LoadedEnvironment {
  int ok;
  char reason[256];
  char *dump;
  char **entries;
  size_t count;
} LoadedEnvironment;

static void free_environment(LoadedEnvironment *loaded) {
  free(loaded->entries);
  free(loaded->dump);
  loaded->entries = NULL;
  loaded->dump = NULL;
  loaded->count = 0U;
}

static LoadedEnvironment source_environment(const char *path, int stderr_fd) {
  static const char script[] =
      ". \"$1\" >&2 || exit 1\n"
      "env -0\n"
      "printf '%s\\0' \"$2\"\n";
  static const char sentinel[] = "__AOS_DIY_ENV_END__";
  static char clean_path[] = "PATH=/usr/local/bin:/usr/bin:/bin";
  LoadedEnvironment loaded;
  int output_pipe[2] = {-1, -1};
  int devnull = -1;
  pid_t child;
  int wait_status = 0;
  size_t dump_size = 0U;
  size_t item_count = 0U;
  size_t index;
  char *argv[7];
  char *environment[2];

  (void)memset(&loaded, 0, sizeof(loaded));
  if (pipe(output_pipe) < 0) {
    (void)snprintf(loaded.reason, sizeof(loaded.reason), "source env pipe: %s",
                   strerror(errno));
    return loaded;
  }
  if (set_close_on_exec(output_pipe[1]) != 0) {
    (void)snprintf(loaded.reason, sizeof(loaded.reason),
                   "source env fcntl: %s", strerror(errno));
    goto cleanup;
  }
  devnull = open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (devnull < 0) {
    (void)snprintf(loaded.reason, sizeof(loaded.reason),
                   "source env /dev/null: %s", strerror(errno));
    goto cleanup;
  }

  argv[0] = (char *)"sh";
  argv[1] = (char *)"-c";
  argv[2] = (char *)script;
  argv[3] = (char *)"sh";
  argv[4] = (char *)path;
  argv[5] = (char *)sentinel;
  argv[6] = NULL;
  environment[0] = clean_path;
  environment[1] = NULL;

  (void)fflush(NULL);
  child = fork();
  if (child < 0) {
    (void)snprintf(loaded.reason, sizeof(loaded.reason), "source env fork: %s",
                   strerror(errno));
    goto cleanup;
  }
  if (child == 0) {
    (void)close(output_pipe[0]);
    if (dup2(devnull, STDIN_FILENO) >= 0 &&
        dup2(output_pipe[1], STDOUT_FILENO) >= 0 &&
        (stderr_fd < 0 || dup2(stderr_fd, STDERR_FILENO) >= 0)) {
      execve("/bin/sh", argv, environment);
    }
    _exit(127);
  }

  (void)close(output_pipe[1]);
  output_pipe[1] = -1;
  for (;;) {
    char chunk[8192];
    ssize_t count = read(output_pipe[0], chunk, sizeof(chunk));
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      (void)snprintf(loaded.reason, sizeof(loaded.reason),
                     "source env read: %s", strerror(errno));
      break;
    }
    if (count == 0) {
      break;
    }
    {
      char *grown = (char *)realloc(loaded.dump, dump_size + (size_t)count);
      if (grown == NULL) {
        (void)snprintf(loaded.reason, sizeof(loaded.reason),
                       "source env: out of memory");
        break;
      }
      loaded.dump = grown;
      (void)memcpy(loaded.dump + dump_size, chunk, (size_t)count);
      dump_size += (size_t)count;
    }
  }

  while (waitpid(child, &wait_status, 0) < 0) {
    if (errno != EINTR) {
      (void)snprintf(loaded.reason, sizeof(loaded.reason),
                     "source env waitpid: %s", strerror(errno));
      break;
    }
  }
  if (loaded.reason[0] != '\0') {
    goto cleanup;
  }
  if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0) {
    (void)snprintf(loaded.reason, sizeof(loaded.reason),
                   "could not source env file: %s", path);
    goto cleanup;
  }
  if (dump_size == 0U || loaded.dump[dump_size - 1U] != '\0') {
    (void)snprintf(loaded.reason, sizeof(loaded.reason),
                   "incomplete environment dump from: %s", path);
    goto cleanup;
  }
  for (index = 0U; index < dump_size; ++index) {
    if (loaded.dump[index] == '\0') {
      ++item_count;
    }
  }
  loaded.entries = (char **)calloc(item_count + 1U, sizeof(*loaded.entries));
  if (loaded.entries == NULL) {
    (void)snprintf(loaded.reason, sizeof(loaded.reason),
                   "source env: out of memory");
    goto cleanup;
  }
  loaded.entries[0] = loaded.dump;
  item_count = 1U;
  for (index = 0U; index + 1U < dump_size; ++index) {
    if (loaded.dump[index] == '\0') {
      loaded.entries[item_count++] = loaded.dump + index + 1U;
    }
  }
  if (item_count == 0U ||
      strcmp(loaded.entries[item_count - 1U], sentinel) != 0) {
    (void)snprintf(loaded.reason, sizeof(loaded.reason),
                   "incomplete environment dump from: %s", path);
    goto cleanup;
  }
  loaded.entries[item_count - 1U] = NULL;
  loaded.count = item_count - 1U;
  loaded.ok = 1;

cleanup:
  close_if_open(output_pipe[0]);
  close_if_open(output_pipe[1]);
  close_if_open(devnull);
  if (!loaded.ok) {
    free_environment(&loaded);
  }
  return loaded;
}

static void report_child_error(int fd, ChildStage stage, int error) {
  ChildError payload;
  const unsigned char *cursor;
  size_t remaining;
  payload.stage = (int)stage;
  payload.error = error;
  cursor = (const unsigned char *)&payload;
  remaining = sizeof(payload);
  while (remaining != 0U) {
    ssize_t count = write(fd, cursor, remaining);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    cursor += (size_t)count;
    remaining -= (size_t)count;
  }
  _exit(127);
}

static void redirect_or_report(int source, int destination, ChildStage stage,
                               int report_fd) {
  if (source >= 0 && dup2(source, destination) < 0) {
    report_child_error(report_fd, stage, errno);
  }
}

/* Returns 1 for a complete child report, 0 for EOF, and -errno on error. */
static int read_child_error(int fd, ChildError *payload) {
  unsigned char *cursor = (unsigned char *)payload;
  size_t received = 0U;
  while (received < sizeof(*payload)) {
    ssize_t count = read(fd, cursor + received, sizeof(*payload) - received);
    if (count == 0) {
      return received == 0U ? 0 : -EIO;
    }
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return -errno;
    }
    received += (size_t)count;
  }
  return 1;
}

static char **copy_pointer_array(size_t count, const char *const *source) {
  char **result;
  size_t index;
  if (count != 0U && source == NULL) {
    return NULL;
  }
  result = (char **)calloc(count + 1U, sizeof(*result));
  if (result == NULL) {
    return NULL;
  }
  for (index = 0U; index < count; ++index) {
    if (source[index] == NULL) {
      free(result);
      return NULL;
    }
    result[index] = (char *)source[index];
  }
  return result;
}

AosOutcome aos_run_process(const AosCall *call) {
  char **argv = NULL;
  static char clean_path[] = "PATH=/usr/local/bin:/usr/bin:/bin";
  char *clean_environment[2] = {clean_path, NULL};
  char **environment = clean_environment;
  LoadedEnvironment loaded;
  int input_fd = -1;
  int output_fd = -1;
  int error_fd = -1;
  int report_pipe[2] = {-1, -1};
  pid_t child;
  int wait_status = 0;
  int report_result;
  ChildError child_error;
  AosOutcome result;

  (void)memset(&loaded, 0, sizeof(loaded));
  if (call == NULL) {
    return aos_outcome_rejected("call is required");
  }
  if (call->argc == 0U || call->argv == NULL || call->argv[0] == NULL ||
      call->argv[0][0] == '\0') {
    return aos_outcome_rejected("argv and argv[0] must not be empty");
  }

  argv = copy_pointer_array(call->argc, call->argv);
  if (argv == NULL) {
    free(argv);
    return aos_outcome_rejected("invalid argv or out of memory");
  }

  input_fd = open_optional_input(call->stdin_path);
  if (call->stdin_path != NULL && input_fd < 0) {
    result = launch_error(errno, "open(stdin)");
    goto cleanup;
  }
  output_fd = open_optional_output(call->stdout_path);
  if (call->stdout_path != NULL && output_fd < 0) {
    result = launch_error(errno, "open(stdout)");
    goto cleanup;
  }
  error_fd = open_optional_output(call->stderr_path);
  if (call->stderr_path != NULL && error_fd < 0) {
    result = launch_error(errno, "open(stderr)");
    goto cleanup;
  }
  if (call->env != NULL) {
    loaded = source_environment(call->env, error_fd);
    if (!loaded.ok) {
      result = aos_outcome_launch_error(0, loaded.reason);
      goto cleanup;
    }
    environment = loaded.entries;
  }
  if (pipe(report_pipe) < 0) {
    result = launch_error(errno, "pipe");
    goto cleanup;
  }
  {
    int error = set_close_on_exec(report_pipe[1]);
    if (error != 0) {
      result = launch_error(error, "fcntl(FD_CLOEXEC)");
      goto cleanup;
    }
  }

  (void)fflush(NULL);
  child = fork();
  if (child < 0) {
    result = launch_error(errno, "fork");
    goto cleanup;
  }
  if (child == 0) {
    (void)close(report_pipe[0]);
    if (call->cwd != NULL && chdir(call->cwd) < 0) {
      report_child_error(report_pipe[1], CHILD_CHDIR, errno);
    }
    redirect_or_report(input_fd, STDIN_FILENO, CHILD_STDIN, report_pipe[1]);
    redirect_or_report(output_fd, STDOUT_FILENO, CHILD_STDOUT, report_pipe[1]);
    redirect_or_report(error_fd, STDERR_FILENO, CHILD_STDERR, report_pipe[1]);
    execve(call->argv[0], argv, environment);
    report_child_error(report_pipe[1], CHILD_EXEC, errno);
  }

  (void)close(report_pipe[1]);
  report_pipe[1] = -1;
  report_result = read_child_error(report_pipe[0], &child_error);
  (void)close(report_pipe[0]);
  report_pipe[0] = -1;

  while (waitpid(child, &wait_status, 0) < 0) {
    if (errno != EINTR) {
      result = aos_outcome_unknown("waitpid failed");
      goto cleanup;
    }
  }
  if (report_result < 0) {
    result = aos_outcome_unknown("could not read exec report pipe");
  } else if (report_result == 1) {
    result = launch_error(child_error.error, stage_name(child_error.stage));
  } else if (WIFEXITED(wait_status)) {
    result = aos_outcome_exited(WEXITSTATUS(wait_status));
  } else if (WIFSIGNALED(wait_status)) {
    result = aos_outcome_signalled(WTERMSIG(wait_status));
  } else {
    result = aos_outcome_unknown("child did not exit or terminate by signal");
  }

cleanup:
  close_if_open(input_fd);
  close_if_open(output_fd);
  close_if_open(error_fd);
  close_if_open(report_pipe[0]);
  close_if_open(report_pipe[1]);
  free(argv);
  free_environment(&loaded);
  return result;
}
