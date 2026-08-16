#include "queue.hpp"

namespace aossimple {

bool Queue::push(Call call) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closed_) return false;
        items_.push_back(std::move(call));
        ++pending_;
    }
    cv_.notify_one();
    return true;
}

std::optional<Call> Queue::take() {
    std::unique_lock<std::mutex> guard(mutex_);
    cv_.wait(guard, [this] { return !items_.empty(); });
    std::optional<Call> front = std::move(items_.front());
    items_.pop_front();
    if (!front) {
        // 哨兵。★ 放回去，這樣重複呼叫 take() 的人也都會拿到「收工了」，
        //   而不是第二次就永遠擋在 wait 上。
        items_.push_front(std::nullopt);
        return std::nullopt;
    }
    --pending_;
    return front;
}

std::optional<Call> Queue::poll() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (items_.empty()) return std::nullopt;
    if (!items_.front()) return std::nullopt;  // 只剩哨兵
    std::optional<Call> front = std::move(items_.front());
    items_.pop_front();
    --pending_;
    return front;
}

void Queue::close() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closed_) return;
        closed_ = true;
        items_.push_back(std::nullopt);
    }
    cv_.notify_all();
}

bool Queue::closed() {
    std::lock_guard<std::mutex> guard(mutex_);
    return closed_;
}

std::size_t Queue::depth() {
    std::lock_guard<std::mutex> guard(mutex_);
    return pending_;
}

}  // namespace aossimple
