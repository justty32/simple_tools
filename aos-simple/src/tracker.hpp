#pragma once
// tracker.hpp — 數「系統裡還有幾件事」，這樣才收得了工。
//
// ── ★★ 為什麼不能只看 queue 空不空 ─────────────────────────────────
//
// 直覺會這樣寫：
//
//     while (queue.depth() != 0) sleep();   // ✗
//     queue.close();
//
// 兩個問題：
//
// 1. **有東西被拿走了但還在跑。** queue 空了不代表沒事做——某條 worker 正拿著
//    一個 Call 在 executor 裡面。這中間關掉，那件事的後續就沒了。
//
// 2. ★★ **處理到一半會生出新工作。** agent loop 派 child 給 exec loop，
//    child 做完又推一個 tick 回 agent loop。所以「兩個 queue 同時是空的」
//    這一瞬間，可能只是工作正好在兩條 loop 之間的半空中。
//
// 所以要數的不是「排著幾個」，是「**進了系統還沒出去的有幾個**」：
//
//     push 成功        -> 計數 +1
//     executor 處理完   -> 計數 -1
//
// 這個算法沒有窗口：一件工作從被 push 的那一刻就算數，到它被完全處理完才不算。
// 而處理途中生出來的新工作，是在**父工作 -1 之前**就 +1 的，所以計數不會在
// 中途假性歸零。歸零就是真的沒事了。

#include <condition_variable>
#include <cstddef>
#include <mutex>

#include "channel.hpp"
#include "loop.hpp"

namespace aossimple {

class WorkTracker {
  public:
    void entered();  // 一件工作進了系統
    void left();     // 一件工作處理完了

    // 擋到計數歸零。★ 這是收工的第一步：等真的沒事了，再去 close 各個 queue。
    void wait_idle();

    unsigned long long in_flight();

    // 包在 Loop／Pool 的 observer 外面：處理完就 -1。
    //
    // ⚠ 順序有意義：**先叫使用者的 observer，再 -1**。
    //   反過來的話，如果使用者的 observer 會推新工作（continuation_observer
    //   就會），計數就有機會先歸零、收工流程提早開始，那個新工作就被丟掉了。
    Loop::Observer wrap(Loop::Observer inner);

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    unsigned long long count_ = 0;
};

// 包在一個 Channel 外面：push 成功就 +1。
//
// ★ Router 要 route 到這個，不是直接 route 到 Channel，否則從外面進來的工作
//   不會被數到。
class TrackedSink : public Sink {
  public:
    TrackedSink(Sink &inner, WorkTracker &tracker) : inner_(inner), tracker_(tracker) {}

    bool push(Call call) override;

  private:
    Sink &inner_;
    WorkTracker &tracker_;
};

}  // namespace aossimple
