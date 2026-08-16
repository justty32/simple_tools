#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace aos_diy::snippets {

// A multi-producer, multi-consumer FIFO queue with drain-on-close semantics.
//
// close() rejects new work but does not discard queued work.  Once the queue is
// both closed and empty, take() returns nullopt immediately on every call.
template <class T>
class BlockingQueue final {
public:
  BlockingQueue() = default;
  BlockingQueue(const BlockingQueue &) = delete;
  BlockingQueue &operator=(const BlockingQueue &) = delete;

  [[nodiscard]] bool push(T value) {
    {
      std::lock_guard lock(mutex_);
      if (closed_) {
        return false;
      }
      items_.push_back(std::move(value));
    }
    ready_.notify_one();
    return true;
  }

  // Blocks until an item is available or the queue has closed and drained.
  [[nodiscard]] std::optional<T> take() {
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [this] { return closed_ || !items_.empty(); });

    if (items_.empty()) {
      return std::nullopt;
    }

    T value = std::move(items_.front());
    items_.pop_front();
    return value;
  }

  // nullopt means "nothing available right now"; use closed() if the caller
  // needs to distinguish an empty open queue from a drained closed queue.
  [[nodiscard]] std::optional<T> try_take() {
    std::lock_guard lock(mutex_);
    if (items_.empty()) {
      return std::nullopt;
    }

    T value = std::move(items_.front());
    items_.pop_front();
    return value;
  }

  void close() {
    {
      std::lock_guard lock(mutex_);
      if (closed_) {
        return;
      }
      closed_ = true;
    }
    ready_.notify_all();
  }

  [[nodiscard]] bool closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
  }

  [[nodiscard]] std::size_t size() const {
    std::lock_guard lock(mutex_);
    return items_.size();
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<T> items_;
  bool closed_ = false;
};

} // namespace aos_diy::snippets
