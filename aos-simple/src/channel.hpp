#pragma once
// channel.hpp — queue 的介面。
//
// ── 為什麼要有這個介面 ──────────────────────────────────────────────
//
// 因為**排序政策屬於各條 loop，不屬於核心**。
//
// 一般 exec 是純 FIFO 就好；但像 llm-ask 那種會自己解析 argv、從 `--priority`
// 決定先後的，需要的是一個會排序的 queue。這兩者對「router 往哪推」和
// 「loop 從哪取」的需求是一樣的，差別只在中間怎麼排。
//
// 所以把它切成介面：Router 只要 Sink，Loop／Pool 只要 Source。
// 各條 loop 自己決定中間放哪一種 Channel，核心不必知道，也不必為了 priority
// 去改 FIFO 那一版。
//
//     Router  --push-->  [ Channel ]  --take-->  Loop / Pool
//                          Queue          純 FIFO
//                          PriorityQueue  排序，優先值由該 loop 自己算

#include <cstddef>
#include <optional>

#include "call.hpp"

namespace aossimple {

// 放東西進去的那一端。
class Sink {
  public:
    virtual ~Sink() = default;

    // 排一個進去。已經收工的話回 false。
    virtual bool push(Call call) = 0;
};

// 拿東西出來的那一端。
class Source {
  public:
    virtual ~Source() = default;

    // 取一個出來，空的話**擋住**等。回 nullopt 代表收工了。
    //
    // ★ 收工之後**重複呼叫都要回 nullopt**，不可以只有第一個呼叫者拿到。
    //   Pool 有 N 條 worker 共吃同一個 Channel，靠的就是這件事當收工廣播。
    virtual std::optional<Call> take() = 0;

    // 取一個出來，空的話**立刻**回 nullopt，不擋。
    // ⚠ 回 nullopt 分不出「空了」和「收工了」——要分清楚就用 take()。
    virtual std::optional<Call> poll() = 0;
};

// 兩端都有的完整 queue。
class Channel : public Sink, public Source {
  public:
    // 叫它收工。★ 語義是「排在前面的做完才停」，不是把待辦丟掉。
    //   重複呼叫要安全。
    virtual void close() = 0;

    virtual bool closed() = 0;
    virtual std::size_t depth() = 0;  // 還排著幾個
};

}  // namespace aossimple
