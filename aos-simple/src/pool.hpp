#pragma once
// pool.hpp — N 條 worker 共吃同一個 queue。
//
// ── 為什麼需要它 ────────────────────────────────────────────────────
//
// 一般的 `Loop` 是一次跑一個，那對「跑外部程式」這件事太保守了：一支跑 30 秒的
// 命令會把後面所有人擋住，而它 29.9 秒都只是在等別人。
//
// 但也不能無上限：exec 會真的開行程、開檔案、吃記憶體。所以是**有上限的併發**，
// 而上限就是 worker 數量——這是用結構保證的，不是靠一個計數器去檢查。
// 同時最多幾個在跑，等於同時最多幾條 worker 執行緒在 `executor.run()` 裡面。
//
//     Pool pool{exec_queue, exec_executor, 4};   // 同時最多 4 個
//
// ── 為什麼可以直接共吃一個 Queue ────────────────────────────────────
//
// 因為 `Queue::take()` 本來就是多執行緒安全的，而且它**取到哨兵會放回去**——
// 所以 N 條 worker 都看得到「收工了」，不會有人永遠擋在那裡。那個當初為了
// 「重複 take 不要卡住」寫的行為，在這裡正好就是收工廣播。
//
// ── 執行緒 ──────────────────────────────────────────────────────────
//
// ★ `Executor` 會被多條執行緒同時呼叫。`ExecExecutor` 沒有狀態所以沒問題；
//   自己寫的執行者要自己管好共用狀態。
// ★ observer 由 Pool 自己上鎖串起來，所以寫 observer 的人不必操心併發。
//   代價是 observer 裡做很慢的事會把 worker 互相拖住。

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "channel.hpp"
#include "loop.hpp"

namespace aossimple {

// exec 那條路的預設併發上限。
inline constexpr std::size_t kDefaultExecWorkers = 4;

class Pool {
  public:
    // 起 workers 條執行緒開始吃 queue。workers 至少是 1。
    Pool(Source &queue, Executor &executor, std::size_t workers,
         Loop::Observer observer = nullptr);
    ~Pool();

    Pool(const Pool &) = delete;
    Pool &operator=(const Pool &) = delete;

    // 等所有 worker 結束。★ 要先 queue.close()，否則會永遠等。
    void join();

    std::size_t workers() const { return threads_.size(); }

    // 各 worker 的統計加總。★ 只有在 join() 之後讀才是穩定的。
    Stats stats() const;

    // 實際觀察到的同時執行數的最高點。用來驗證上限真的有效。
    std::size_t peak_in_flight() const { return peak_.load(); }

  private:
    // 內部包一層：數同時有幾個在 executor 裡面，並把 observer 串起來。
    class Tracking;

    Source &queue_;
    std::atomic<std::size_t> in_flight_{0};
    std::atomic<std::size_t> peak_{0};
    std::mutex observer_mutex_;
    Loop::Observer observer_;

    std::unique_ptr<Tracking> tracking_;
    std::vector<std::unique_ptr<Loop>> loops_;
    std::vector<std::thread> threads_;
    bool joined_ = false;
};

}  // namespace aossimple
