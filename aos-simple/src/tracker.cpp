#include "tracker.hpp"

namespace aossimple {

void WorkTracker::entered() {
    std::lock_guard<std::mutex> guard(mutex_);
    ++count_;
}

void WorkTracker::left() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (count_ == 0) return;  // 不該發生；防呆，不要繞回去變成天文數字
        --count_;
        if (count_ != 0) return;
    }
    cv_.notify_all();
}

void WorkTracker::wait_idle() {
    std::unique_lock<std::mutex> guard(mutex_);
    cv_.wait(guard, [this] { return count_ == 0; });
}

unsigned long long WorkTracker::in_flight() {
    std::lock_guard<std::mutex> guard(mutex_);
    return count_;
}

Loop::Observer WorkTracker::wrap(Loop::Observer inner) {
    WorkTracker *self = this;
    return [self, inner](const Call &call, const Outcome &outcome) {
        // ★ 先跑使用者的 observer（它可能會推新工作，那要在 -1 之前先 +1），
        //   再減。順序反了會讓計數提早歸零。
        if (inner) inner(call, outcome);
        self->left();
    };
}

bool TrackedSink::push(Call call) {
    // ★ 先 +1 再 push。反過來的話，push 之後、+1 之前，那件工作可能已經被別條
    //   worker 拿走做完並 -1 了 —— 計數就會變成負的（實作上是被防呆吃掉，
    //   結果是永遠等不到 idle）。
    tracker_.entered();
    if (!inner_.push(std::move(call))) {
        tracker_.left();  // 沒收下，還回去
        return false;
    }
    return true;
}

}  // namespace aossimple
