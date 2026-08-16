#pragma once

#include "blocking_queue.hpp"
#include "call.hpp"
#include "outcome.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <string>
#include <utility>

namespace aos_diy::snippets {

class Executor {
public:
  virtual ~Executor() = default;
  virtual Outcome run(const Call &call) = 0;
};

struct LoopStats {
  std::size_t handled = 0;
  std::size_t rejected = 0;
  std::size_t executor_exceptions = 0;
};

class EventLoop final {
public:
  using Observer = std::function<void(const Call &, const Outcome &)>;

  EventLoop(BlockingQueue<Call> &queue, Executor &executor,
            Observer observer = {})
      : queue_(queue), executor_(executor), observer_(std::move(observer)) {}

  // Runs until close() has been called and all earlier work has drained.
  void run() {
    while (auto call = queue_.take()) {
      handle(*call);
    }
  }

  // Handles at most one currently available item without blocking.
  [[nodiscard]] bool step() {
    auto call = queue_.try_take();
    if (!call) {
      return false;
    }
    handle(*call);
    return true;
  }

  [[nodiscard]] std::size_t drain() {
    std::size_t count = 0;
    while (step()) {
      ++count;
    }
    return count;
  }

  [[nodiscard]] const LoopStats &stats() const noexcept { return stats_; }

private:
  void handle(const Call &call) {
    ++stats_.handled;
    Outcome outcome;

    try {
      outcome = executor_.run(call);
    } catch (const std::exception &error) {
      ++stats_.executor_exceptions;
      outcome = Outcome::unknown(std::string{"executor threw: "} + error.what());
    } catch (...) {
      ++stats_.executor_exceptions;
      outcome = Outcome::unknown("executor threw a non-standard exception");
    }

    if (outcome.status == Status::rejected) {
      ++stats_.rejected;
    }
    if (observer_) {
      observer_(call, outcome);
    }
  }

  BlockingQueue<Call> &queue_;
  Executor &executor_;
  Observer observer_;
  LoopStats stats_;
};

} // namespace aos_diy::snippets
