#include "atomic_publish.hpp"
#include "event_loop.hpp"
#include "posix_process.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace aos_diy::snippets;

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string read_file(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

fs::path temporary_directory() {
  std::string pattern = "/tmp/aos-diy-snippets.XXXXXX";
  char *path = ::mkdtemp(pattern.data());
  if (path == nullptr) {
    throw std::runtime_error("mkdtemp failed");
  }
  return path;
}

class RecordingExecutor final : public Executor {
public:
  Outcome run(const Call &call) override {
    seen.push_back(call.argv.at(0));
    if (call.argv.at(0) == "throw") {
      throw std::runtime_error("deliberate test exception");
    }
    return Outcome::exited(0);
  }

  std::vector<std::string> seen;
};

Call call_named(std::string name) {
  Call call;
  call.argv.push_back(std::move(name));
  return call;
}

void test_queue_and_loop() {
  BlockingQueue<Call> queue;
  RecordingExecutor executor;
  std::vector<Status> statuses;
  EventLoop loop{queue, executor,
                 [&statuses](const Call &, const Outcome &outcome) {
                   statuses.push_back(outcome.status);
                 }};

  require(queue.push(call_named("first")), "first push failed");
  require(queue.push(call_named("throw")), "second push failed");
  queue.close();
  require(!queue.push(call_named("late")), "closed queue accepted work");

  loop.run();
  require(executor.seen == std::vector<std::string>{"first", "throw"},
          "close did not drain in FIFO order");
  require(statuses == std::vector<Status>{Status::exited, Status::unknown},
          "loop did not translate executor exception");
  require(loop.stats().handled == 2 && loop.stats().executor_exceptions == 1,
          "loop stats are wrong");
  require(!queue.take(), "drained closed queue did not stay closed");
}

void test_multiple_consumers() {
  BlockingQueue<int> queue;
  std::atomic<int> count = 0;
  std::atomic<int> sum = 0;
  std::vector<std::thread> workers;

  for (int worker = 0; worker < 4; ++worker) {
    workers.emplace_back([&queue, &count, &sum] {
      while (auto value = queue.take()) {
        count.fetch_add(1, std::memory_order_relaxed);
        sum.fetch_add(*value, std::memory_order_relaxed);
      }
    });
  }
  for (int value = 1; value <= 100; ++value) {
    require(queue.push(value), "concurrent queue rejected work");
  }
  queue.close();
  for (std::thread &worker : workers) {
    worker.join();
  }

  require(count.load(std::memory_order_relaxed) == 100,
          "workers lost or duplicated an item");
  require(sum.load(std::memory_order_relaxed) == 5050,
          "workers consumed the wrong values");
}

void test_atomic_publish(const fs::path &directory) {
  const fs::path result = directory / "exit";
  require(!publish_atomically(result, "17\n"), "first publish failed");
  require(read_file(result) == "17\n", "first publish has wrong contents");
  require(!publish_atomically(result, "0\n"), "replacement publish failed");
  require(read_file(result) == "0\n", "replacement was not atomic overwrite");
}

void test_process(const fs::path &directory) {
  Call command;
  command.argv = {"/bin/sh", "-c", "printf 'hello'; exit 7"};
  command.stdout_path = (directory / "stdout").string();
  command.stderr_path = (directory / "stderr").string();
  command.cwd = directory.string();

  const Outcome exited = run_process(command);
  require(exited.status == Status::exited && exited.code == 7,
          "normal process result is wrong");
  require(read_file(*command.stdout_path) == "hello",
          "stdout redirection is wrong");

  command.argv = {"/definitely/not/a/program"};
  const Outcome missing = run_process(command);
  require(missing.status == Status::launch_error && missing.error == ENOENT,
          "exec failure was not identified as a launch error");

  command.argv = {"/bin/sh", "-c", "exit 127"};
  const Outcome explicit_127 = run_process(command);
  require(explicit_127.status == Status::exited && explicit_127.code == 127,
          "program exit 127 was confused with exec failure");

  command.argv = {"/bin/sh", "-c", "kill -TERM $$"};
  const Outcome signalled = run_process(command);
  require(signalled.status == Status::signalled && signalled.signal == SIGTERM,
          "signal termination was flattened into an exit code");

  const fs::path env_path = directory / "environment";
  std::ofstream env_file(env_path);
  env_file << "export AOS_DIY_GREETING='from-env'\n";
  env_file.close();
  command.env = env_path.string();
  command.argv = {"/bin/sh", "-c", "printf '%s:%s' \"$AOS_DIY_GREETING\" \"$PWD\""};
  const Outcome configured = run_process(command);
  require(configured.status == Status::exited && configured.code == 0,
          "env/cwd configured process did not exit normally");
  require(read_file(*command.stdout_path) ==
              "from-env:" + directory.string(),
          "Call env or cwd did not reach exec");
}

} // namespace

int main() {
  const fs::path directory = temporary_directory();
  test_queue_and_loop();
  test_multiple_consumers();
  test_atomic_publish(directory);
  test_process(directory);
  fs::remove_all(directory);
}
