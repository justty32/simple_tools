#include "atomic_publish.hpp"
#include "event_loop.hpp"
#include "posix_process.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace aos_diy::snippets11;

namespace {

int failures = 0;

void require(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
  }
}

std::string read_file(const std::string &path) {
  std::ifstream input(path.c_str(), std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

class RecordingExecutor : public Executor {
public:
  Outcome run(const Call &call) {
    seen.push_back(call.argv.at(0U));
    if (call.argv.at(0U) == "throw") {
      throw std::runtime_error("deliberate test exception");
    }
    return Outcome::exited(0);
  }
  std::vector<std::string> seen;
};

Call call_named(const std::string &name) {
  Call call;
  call.argv.push_back(name);
  return call;
}

void test_queue_and_loop() {
  BlockingQueue<Call> queue;
  RecordingExecutor executor;
  std::vector<Status> statuses;
  EventLoop loop(queue, executor,
                 [&statuses](const Call &, const Outcome &outcome) {
                   statuses.push_back(outcome.status);
                 });

  require(queue.push(call_named("first")), "first push");
  require(queue.push(call_named("throw")), "second push");
  queue.close();
  require(!queue.push(call_named("late")), "closed queue accepted work");
  loop.run();

  require(executor.seen.size() == 2U && executor.seen[0U] == "first" &&
              executor.seen[1U] == "throw",
          "close did not drain in FIFO order");
  require(statuses.size() == 2U && statuses[0U] == Status::exited &&
              statuses[1U] == Status::unknown,
          "executor exception was not contained");
  require(loop.stats().handled == 2U &&
              loop.stats().executor_exceptions == 1U,
          "loop stats");
}

void test_multiple_consumers() {
  BlockingQueue<int> queue;
  std::atomic<int> count(0);
  std::atomic<int> sum(0);
  std::vector<std::thread> workers;
  for (int worker = 0; worker < 4; ++worker) {
    workers.push_back(std::thread([&queue, &count, &sum] {
      int value = 0;
      while (queue.take(value) == TakeResult::item) {
        count.fetch_add(1, std::memory_order_relaxed);
        sum.fetch_add(value, std::memory_order_relaxed);
      }
    }));
  }
  for (int value = 1; value <= 100; ++value) {
    require(queue.push(value), "concurrent push");
  }
  queue.close();
  for (std::size_t i = 0U; i < workers.size(); ++i) {
    workers[i].join();
  }
  require(count.load(std::memory_order_relaxed) == 100 &&
              sum.load(std::memory_order_relaxed) == 5050,
          "workers lost or duplicated values");
}

void test_files_and_process(const std::string &directory) {
  const std::string exit_path = directory + "/exit";
  const std::string stdout_path = directory + "/stdout";
  const std::string stderr_path = directory + "/stderr";
  require(!publish_atomically(exit_path, "17\n"), "atomic publish");
  require(read_file(exit_path) == "17\n", "atomic publish contents");

  Call call;
  call.argv = {"/bin/sh", "-c", "printf hello; exit 7"};
  call.stdout_path = stdout_path;
  call.stderr_path = stderr_path;
  call.cwd = directory;

  Outcome outcome = run_process(call);
  require(outcome.status == Status::exited && outcome.code == 7,
          "normal exit");
  require(read_file(stdout_path) == "hello", "stdout redirect");

  call.argv = {"/definitely/not/a/program"};
  outcome = run_process(call);
  require(outcome.status == Status::launch_error && outcome.error == ENOENT,
          "exec failure detection");

  call.argv = {"/bin/sh", "-c", "exit 127"};
  outcome = run_process(call);
  require(outcome.status == Status::exited && outcome.code == 127,
          "exit 127 confused with exec failure");

  call.argv = {"/bin/sh", "-c", "kill -TERM $$"};
  outcome = run_process(call);
  require(outcome.status == Status::signalled && outcome.signal == SIGTERM,
          "signal flattened into exit code");

  const std::string env_path = directory + "/environment";
  {
    std::ofstream env_file(env_path.c_str());
    env_file << "export AOS_DIY_GREETING='from-env'\n";
  }
  call.env = env_path;
  call.argv = {"/bin/sh", "-c",
               "printf '%s:%s' \"$AOS_DIY_GREETING\" \"$PWD\""};
  outcome = run_process(call);
  require(outcome.status == Status::exited && outcome.code == 0,
          "env/cwd configured process did not exit normally");
  require(read_file(stdout_path) == "from-env:" + directory,
          "Call env or cwd did not reach exec");

  (void)::unlink(env_path.c_str());
  (void)::unlink(exit_path.c_str());
  (void)::unlink(stdout_path.c_str());
  (void)::unlink(stderr_path.c_str());
}

} // namespace

int main() {
  char pattern[] = "/tmp/aos-diy-cpp11.XXXXXX";
  char *directory = ::mkdtemp(pattern);
  require(directory != NULL, "mkdtemp");
  test_queue_and_loop();
  test_multiple_consumers();
  if (directory != NULL) {
    test_files_and_process(directory);
    (void)::rmdir(directory);
  }
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
