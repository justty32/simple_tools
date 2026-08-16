#include "atomic_publish.h"
#include "event_loop.h"
#include "posix_process.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define REQUIRE(condition, message)                                           \
  do {                                                                        \
    if (!(condition)) {                                                       \
      (void)fprintf(stderr, "FAIL: %s\n", (message));                       \
      ++failures;                                                             \
    }                                                                         \
  } while (0)

typedef struct Recorder {
  const char *seen[4];
  size_t count;
} Recorder;

static AosOutcome record_call(const AosCall *call, void *context) {
  Recorder *recorder = (Recorder *)context;
  recorder->seen[recorder->count++] = call->argv[0];
  return aos_outcome_exited(0);
}

static void test_queue_and_loop(void) {
  const char *first_argv[] = {"first"};
  const char *second_argv[] = {"second"};
  AosCall first = {1U, first_argv, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
  AosCall second = {1U, second_argv, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
  AosQueue queue;
  AosEventLoop loop;
  Recorder recorder;
  (void)memset(&recorder, 0, sizeof(recorder));

  REQUIRE(aos_queue_init(&queue) == 0, "queue init");
  REQUIRE(aos_queue_push(&queue, &first) == 0, "first push");
  REQUIRE(aos_queue_push(&queue, &second) == 0, "second push");
  REQUIRE(aos_queue_close(&queue) == 0, "queue close");
  REQUIRE(aos_queue_push(&queue, &first) == ECANCELED,
          "closed queue accepted work");

  aos_loop_init(&loop, &queue, record_call, &recorder);
  REQUIRE(aos_loop_run(&loop) == 0, "loop run");
  REQUIRE(recorder.count == 2U && strcmp(recorder.seen[0], "first") == 0 &&
              strcmp(recorder.seen[1], "second") == 0,
          "close did not drain in FIFO order");
  REQUIRE(loop.stats.handled == 2U, "loop stats");
  REQUIRE(aos_queue_destroy(&queue) == 0, "queue destroy");
}

typedef struct Worker {
  AosQueue *queue;
  int count;
  int sum;
} Worker;

static void *consume(void *context) {
  Worker *worker = (Worker *)context;
  void *value = NULL;
  while (aos_queue_take(worker->queue, &value) == AOS_TAKE_ITEM) {
    ++worker->count;
    worker->sum += *(const int *)value;
  }
  return NULL;
}

static void test_multiple_consumers(void) {
  AosQueue queue;
  pthread_t threads[4];
  Worker workers[4];
  int values[100];
  int count = 0;
  int sum = 0;
  size_t index;
  REQUIRE(aos_queue_init(&queue) == 0, "concurrent queue init");
  for (index = 0U; index < 4U; ++index) {
    workers[index].queue = &queue;
    workers[index].count = 0;
    workers[index].sum = 0;
    REQUIRE(pthread_create(&threads[index], NULL, consume, &workers[index]) == 0,
            "pthread_create");
  }
  for (index = 0U; index < 100U; ++index) {
    values[index] = (int)index + 1;
    REQUIRE(aos_queue_push(&queue, &values[index]) == 0, "concurrent push");
  }
  REQUIRE(aos_queue_close(&queue) == 0, "concurrent close");
  for (index = 0U; index < 4U; ++index) {
    REQUIRE(pthread_join(threads[index], NULL) == 0, "pthread_join");
    count += workers[index].count;
    sum += workers[index].sum;
  }
  REQUIRE(count == 100 && sum == 5050, "workers lost or duplicated values");
  REQUIRE(aos_queue_destroy(&queue) == 0, "concurrent queue destroy");
}

static int read_text(const char *path, char *buffer, size_t capacity) {
  FILE *file = fopen(path, "rb");
  size_t count;
  if (file == NULL || capacity == 0U) {
    return -1;
  }
  count = fread(buffer, 1U, capacity - 1U, file);
  buffer[count] = '\0';
  (void)fclose(file);
  return 0;
}

static void test_files_and_process(const char *directory) {
  char result_path[512];
  char stdout_path[512];
  char stderr_path[512];
  char env_path[512];
  char buffer[512];
  const char *argv[] = {"/bin/sh", "-c", "printf hello; exit 7"};
  const char *missing_argv[] = {"/definitely/not/a/program"};
  const char *exit_127_argv[] = {"/bin/sh", "-c", "exit 127"};
  const char *signal_argv[] = {"/bin/sh", "-c", "kill -TERM $$"};
  const char *configured_argv[] = {
      "/bin/sh", "-c",
      "printf '%s:%s' \"$AOS_DIY_GREETING\" \"$PWD\""};
  AosCall call;
  AosOutcome outcome;

  (void)snprintf(result_path, sizeof(result_path), "%s/exit", directory);
  (void)snprintf(stdout_path, sizeof(stdout_path), "%s/stdout", directory);
  (void)snprintf(stderr_path, sizeof(stderr_path), "%s/stderr", directory);
  (void)snprintf(env_path, sizeof(env_path), "%s/environment", directory);
  REQUIRE(aos_publish_atomically(result_path, "17\n", 3U) == 0,
          "atomic publish");
  REQUIRE(read_text(result_path, buffer, sizeof(buffer)) == 0 &&
              strcmp(buffer, "17\n") == 0,
          "atomic publish contents");

  (void)memset(&call, 0, sizeof(call));
  call.argc = 3U;
  call.argv = argv;
  call.stdout_path = stdout_path;
  call.stderr_path = stderr_path;
  call.cwd = directory;

  outcome = aos_run_process(&call);
  REQUIRE(outcome.status == AOS_EXITED && outcome.code == 7, "normal exit");
  REQUIRE(read_text(stdout_path, buffer, sizeof(buffer)) == 0 &&
              strcmp(buffer, "hello") == 0,
          "stdout redirect");

  call.argc = 1U;
  call.argv = missing_argv;
  outcome = aos_run_process(&call);
  REQUIRE(outcome.status == AOS_LAUNCH_ERROR && outcome.error_number == ENOENT,
          "exec failure detection");

  call.argc = 3U;
  call.argv = exit_127_argv;
  outcome = aos_run_process(&call);
  REQUIRE(outcome.status == AOS_EXITED && outcome.code == 127,
          "exit 127 confused with exec failure");

  call.argv = signal_argv;
  outcome = aos_run_process(&call);
  REQUIRE(outcome.status == AOS_SIGNALLED && outcome.signal_number == SIGTERM,
          "signal flattened into exit code");

  {
    FILE *env_file = fopen(env_path, "wb");
    REQUIRE(env_file != NULL, "create env file");
    if (env_file != NULL) {
      (void)fputs("export AOS_DIY_GREETING='from-env'\n", env_file);
      (void)fclose(env_file);
    }
  }
  call.env = env_path;
  call.argv = configured_argv;
  outcome = aos_run_process(&call);
  REQUIRE(outcome.status == AOS_EXITED && outcome.code == 0,
          "env/cwd configured process did not exit normally");
  (void)snprintf(buffer, sizeof(buffer), "from-env:%s", directory);
  {
    char actual[512];
    REQUIRE(read_text(stdout_path, actual, sizeof(actual)) == 0 &&
                strcmp(actual, buffer) == 0,
            "Call env or cwd did not reach exec");
  }

  (void)unlink(env_path);
  (void)unlink(result_path);
  (void)unlink(stdout_path);
  (void)unlink(stderr_path);
}

int main(void) {
  char pattern[] = "/tmp/aos-diy-c99.XXXXXX";
  char *directory = mkdtemp(pattern);
  REQUIRE(directory != NULL, "mkdtemp");
  test_queue_and_loop();
  test_multiple_consumers();
  if (directory != NULL) {
    test_files_and_process(directory);
    (void)rmdir(directory);
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
