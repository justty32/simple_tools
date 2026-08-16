#include "loop.hpp"

#include <exception>

namespace aossimple {

const char *to_string(Status status) {
    switch (status) {
        case Status::Rejected: return "rejected";
        case Status::Exited: return "exited";
        case Status::Signalled: return "signalled";
        case Status::LaunchError: return "launch-error";
        case Status::Unknown: return "unknown";
    }
    return "unknown";
}

Outcome Outcome::rejected(std::string why) {
    Outcome o;
    o.status = Status::Rejected;
    o.reason = std::move(why);
    return o;
}

Outcome Outcome::exited(int code) {
    Outcome o;
    o.status = Status::Exited;
    o.code = code;
    return o;
}

Outcome Outcome::signalled(int signal) {
    Outcome o;
    o.status = Status::Signalled;
    o.signal = signal;
    return o;
}

Outcome Outcome::launch_error(int errnum, std::string why) {
    Outcome o;
    o.status = Status::LaunchError;
    o.err = errnum;
    o.reason = std::move(why);
    return o;
}

Outcome Outcome::unknown(std::string why) {
    Outcome o;
    o.status = Status::Unknown;
    o.reason = std::move(why);
    return o;
}

Outcome DefaultExecutor::run(const Call &) {
    return Outcome::unknown("沒有接執行者");
}

Loop::Loop(Source &queue, Executor &executor, Observer observer)
    : queue_(queue), executor_(executor), observer_(std::move(observer)) {}

void Loop::handle(const Call &call) {
    ++stats_.handled;

    Outcome outcome;
    try {
        outcome = executor_.run(call);
    } catch (const std::exception &error) {
        // 執行者丟例外是它的 bug。這裡接住是為了「一個壞呼叫炸不掉整條 loop」，
        // 但結論只能是 Unknown——我們不知道它做到哪裡了，也就不知道能不能重跑。
        outcome = Outcome::unknown(std::string{"執行者丟出例外："} + error.what());
    } catch (...) {
        outcome = Outcome::unknown("執行者丟出了一個不是 std::exception 的東西");
    }

    if (outcome.status == Status::Rejected) ++stats_.rejected;
    if (observer_) observer_(call, outcome);
}

void Loop::run() {
    for (;;) {
        std::optional<Call> call = queue_.take();
        if (!call) break;  // 哨兵
        handle(*call);
    }
}

bool Loop::step() {
    std::optional<Call> call = queue_.poll();
    if (!call) return false;
    handle(*call);
    return true;
}

std::size_t Loop::drain() {
    std::size_t done = 0;
    while (step()) ++done;
    return done;
}

}  // namespace aossimple
