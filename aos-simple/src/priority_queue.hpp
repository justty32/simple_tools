#pragma once
// priority_queue.hpp — 會排序的 Channel。優先值**由使用它的那條 loop 自己算**。
//
// ── 核心不認識「優先權」這件事 ──────────────────────────────────────
//
// `Call` 裡沒有 priority 欄位，也不會有：那是各條 loop 的政策，不是呼叫的形狀。
// llm-ask 從 `--priority` 讀、build 那條可能看 user 是誰、exec 那條可能根本不排序。
// 所以這裡收一個函式，怎麼算是呼叫端的事：
//
//     PriorityQueue q{[](const Call &c) {
//         // llm-ask 自己的 argparse：--priority N
//         return parse_priority(c.argv);
//     }};
//
// ★ 數字**大的先做**。
//
// ── ⚠ 同分一定要 FIFO ───────────────────────────────────────────────
//
// std::priority_queue 對同分的相對順序沒有保證，那會讓「一堆同優先權的工作」
// 的順序變成不可預測——實務上就是有人會餓死，而且重現不了。
// 所以這裡自己配一個遞增的序號當第二鍵：同分照先來後到。
//
// ── ⚠ 優先權不會讓正在跑的讓位 ──────────────────────────────────────
//
// 排序只發生在「還排在 queue 裡」的東西之間。已經被 worker 取走的不會被搶佔，
// 所以一個低優先權的長工作還是會佔住一條 worker 直到它結束（或逾時）。
// 要搶佔就得能砍掉跑到一半的工作，那是另一件事。

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>

#include "channel.hpp"

namespace aossimple {

class PriorityQueue : public Channel {
  public:
    // 算優先值的函式。數字大的先做。給 nullptr 就是全部同分（退化成 FIFO）。
    using Priority = std::function<long(const Call &)>;

    explicit PriorityQueue(Priority priority = nullptr);

    bool push(Call call) override;
    std::optional<Call> take() override;
    std::optional<Call> poll() override;
    void close() override;
    bool closed() override;
    std::size_t depth() override;

  private:
    struct Entry {
        long priority = 0;
        unsigned long long seq = 0;  // 同分時的第二鍵：先進的先出
        Call call;
    };

    // 回傳 true 代表 a 排在 b **後面**（std::priority_queue 是最大堆，
    // 比較函式要講的是「a 的優先度比 b 低嗎」）。
    struct Later {
        bool operator()(const Entry &a, const Entry &b) const {
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.seq > b.seq;  // 同分：序號小的（先來的）排前面
        }
    };

    std::optional<Call> pop_locked();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<Entry, std::vector<Entry>, Later> items_;
    Priority priority_;
    unsigned long long next_seq_ = 0;
    bool closed_ = false;
};

}  // namespace aossimple
