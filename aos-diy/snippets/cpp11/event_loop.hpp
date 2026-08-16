#pragma once

#include "blocking_queue.hpp"
#include "call.hpp"
#include "outcome.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <string>

namespace aos_diy {
namespace snippets11 {

class Executor {
public:
  virtual ~Executor() {}
  virtual Outcome run(const Call &call) = 0;
};

struct LoopStats {
  LoopStats() : handled(0U), rejected(0U), executor_exceptions(0U) {}
  std::size_t handled;
  std::size_t rejected;
  std::size_t executor_exceptions;
};

class EventLoop {
public:
  typedef std::function<void(const Call &, const Outcome &)> Observer;

  EventLoop(BlockingQueue<Call> &queue, Executor &executor,
            const Observer &observer = Observer())
      : queue_(queue), executor_(executor), observer_(observer) {}

  void run() {
    Call call;
    while (queue_.take(call) == TakeResult::item) {
      handle(call);
    }
  }

  bool step() {
    Call call;
    if (queue_.try_take(call) != TakeResult::item) {
      return false;
    }
    handle(call);
    return true;
  }

  std::size_t drain() {
    std::size_t count = 0U;
    while (step()) {
      ++count;
    }
    return count;
  }

  const LoopStats &stats() const { return stats_; }

private:
  void handle(const Call &call) {
    ++stats_.handled;
    Outcome outcome;
    try {
      outcome = executor_.run(call);
    } catch (const std::exception &error) {
      ++stats_.executor_exceptions;
      outcome = Outcome::unknown(std::string("executor threw: ") + error.what());
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

} // namespace snippets11
} // namespace aos_diy
