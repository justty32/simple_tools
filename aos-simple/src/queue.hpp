#pragma once
// queue.hpp — 呼叫佇列。FIFO，多個生產者，單一消費者。
//
// ── 收工用哨兵，不是「關掉就算」──────────────────────────────────
//
// close() 之後**排在前面的照做完**，取到哨兵才回「沒有了」。
// 「叫它收工」和「把還沒做的丟掉」是兩件事，這裡只做前者。
//
// ── 為什麼有鎖 ──────────────────────────────────────────────────────
//
// 現在還沒有第二條執行緒，但佇列是遲早會被別人餵的東西（socket、監看目錄、
// 排程器）。把鎖放在一開始，比之後「這裡好像該加鎖」再回頭補要安全得多——
// 而且單執行緒下這個成本量不出來。

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

#include "call.hpp"
#include "channel.hpp"

namespace aossimple {

class Queue : public Channel {
  public:
    // 排一個進去。已經 close 的話回 false，呼叫端自己決定要當錯誤還是忽略。
    bool push(Call call) override;

    // 取一個出來，空的話**擋住**等。回 nullopt 代表收工了（取到哨兵）。
    std::optional<Call> take() override;

    // 取一個出來，空的話**立刻**回 nullopt，不擋。
    // ⚠ 回 nullopt 分不出「空了」和「收工了」——要分清楚就用 take()。
    std::optional<Call> poll() override;

    // 往隊尾放哨兵。重複呼叫是安全的（只放一個）。
    void close() override;

    bool closed() override;
    std::size_t depth() override;  // 還排著幾個，不含哨兵

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    // ★ 用 optional<Call> 當元素：nullopt 就是哨兵。這樣哨兵不可能跟任何一個
    //   真的 Call 撞在一起，也不必另外開一個旗標去記「哨兵在第幾個」。
    std::deque<std::optional<Call>> items_;
    bool closed_ = false;
    std::size_t pending_ = 0;  // 不含哨兵
};

}  // namespace aossimple
