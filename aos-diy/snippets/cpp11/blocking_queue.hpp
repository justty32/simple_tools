#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace aos_diy {
namespace snippets11 {

enum class TakeResult { item, empty, closed };

template <class T>
class BlockingQueue {
public:
  BlockingQueue() : closed_(false) {}

  bool push(T value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) {
        return false;
      }
      items_.push_back(std::move(value));
    }
    ready_.notify_one();
    return true;
  }

  TakeResult take(T &value) {
    std::unique_lock<std::mutex> lock(mutex_);
    ready_.wait(lock, [this] { return closed_ || !items_.empty(); });
    if (items_.empty()) {
      return TakeResult::closed;
    }
    value = std::move(items_.front());
    items_.pop_front();
    return TakeResult::item;
  }

  TakeResult try_take(T &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (items_.empty()) {
      return closed_ ? TakeResult::closed : TakeResult::empty;
    }
    value = std::move(items_.front());
    items_.pop_front();
    return TakeResult::item;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    ready_.notify_all();
  }

  bool closed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
  }

private:
  BlockingQueue(const BlockingQueue &);
  BlockingQueue &operator=(const BlockingQueue &);

  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<T> items_;
  bool closed_;
};

} // namespace snippets11
} // namespace aos_diy
