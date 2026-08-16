#include "priority_queue.hpp"

namespace aossimple {

PriorityQueue::PriorityQueue(Priority priority) : priority_(std::move(priority)) {}

bool PriorityQueue::push(Call call) {
    // ★ 優先值在**鎖外面**算：那是使用者給的函式，可能要 parse argv，
    //   不該讓它拖著整個 queue 的鎖。
    const long value = priority_ ? priority_(call) : 0;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closed_) return false;
        items_.push(Entry{value, next_seq_++, std::move(call)});
    }
    cv_.notify_one();
    return true;
}

std::optional<Call> PriorityQueue::pop_locked() {
    if (items_.empty()) return std::nullopt;
    // ⚠ top() 是 const 參考，所以要 move 得先複製出來再 pop。
    //   priority_queue 沒有提供可以 move 的存取方式，這是它的已知不便。
    Entry entry = items_.top();
    items_.pop();
    return std::move(entry.call);
}

std::optional<Call> PriorityQueue::take() {
    std::unique_lock<std::mutex> guard(mutex_);
    // 這裡不像 Queue 那樣放哨兵進容器——排序容器裡放哨兵的話它會被優先值
    // 排到不知道哪裡去，「排在前面的做完才停」就不成立了。改成用旗標：
    // 收工 = closed_ 且已經空了。
    cv_.wait(guard, [this] { return !items_.empty() || closed_; });
    if (!items_.empty()) return pop_locked();
    return std::nullopt;  // closed_ 且空了 = 收工。重複呼叫也都會回這個。
}

std::optional<Call> PriorityQueue::poll() {
    std::lock_guard<std::mutex> guard(mutex_);
    return pop_locked();
}

void PriorityQueue::close() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closed_) return;
        closed_ = true;
    }
    // ★ notify_all 不是 notify_one：Pool 有 N 條 worker 全部擋在 wait 上，
    //   只叫醒一條的話其他的會永遠睡著。
    cv_.notify_all();
}

bool PriorityQueue::closed() {
    std::lock_guard<std::mutex> guard(mutex_);
    return closed_;
}

std::size_t PriorityQueue::depth() {
    std::lock_guard<std::mutex> guard(mutex_);
    return items_.size();
}

}  // namespace aossimple
