#pragma once
// loop.hpp — 具名的 loop：一條 loop = 一條執行緒 + 一個 Janet VM + 一張長存 env
//            + 一個 queue。
//
// ★ 這個檔完全不認識 Janet。它只知道「有一條執行緒會把 Job 做完」。
//   Janet 的部分全部關在 vm.cpp 裡，因為 VM 狀態是 thread-local，
//   而這裡是唯一決定「哪條執行緒」的地方。
//
// ── 為什麼是具名、動態建立 ──────────────────────────────────────────
//
// 因為每條 loop 有自己的長存 env。固定 N 條 worker + round-robin 的話，
// 任務落到哪條不確定，那 env 就不能拿來存狀態了——而長存 env 正是這整套的重點。
// 具名之後 `agent-7` 這個名字就對應到一張確定的 env：
//
//     aos lisp eval agent-7 '(def x 7)'
//     aos lisp eval agent-7 'x'          => 7
//     aos lisp eval build   'x'          => 錯，那是另一張 env
//
// ── 收工 ────────────────────────────────────────────────────────────
//
// queue 用一個哨兵（nullptr job）收工，不是「關掉就算」：排在哨兵前面的 job
// 照做完。這跟 aos-core 的 `aos daemon stop`（做完手上的事之後才收工）一致。

#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "job.hpp"

namespace aoslisp {

class Loop {
  public:
    explicit Loop(std::string name);
    ~Loop();

    Loop(const Loop &) = delete;
    Loop &operator=(const Loop &) = delete;

    const std::string &name() const { return name_; }

    // 排一個 job 進去。已經在收工的話回 false（job 不會被做）。
    bool submit(const JobPtr &job);

    // 放哨兵，叫它做完手上排著的就收工。可以重複呼叫。
    void request_stop();

    // 等執行緒真的結束。★ request_stop 之後才呼叫，否則會永遠等。
    void join();

    unsigned long long handled();
    std::size_t depth();

  private:
    void body();  // 執行緒本體，唯一碰 Vm 的地方

    const std::string name_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<JobPtr> queue_;  // 含哨兵（空的 shared_ptr）
    bool closed_ = false;
    unsigned long long handled_ = 0;
    std::thread thread_;
};

struct LoopInfo {
    std::string name;
    unsigned long long handled;
    std::size_t depth;
};

// 全部 loop 的登記處。★ plugin.h 說同一個 run() 可能同時被多條連線執行緒呼叫，
// 所以這裡的每一個操作都要鎖。
class Registry {
  public:
    // 找到就用，沒有就開一條。
    Loop &acquire(const std::string &name);
    // 只找不開。
    Loop *find(const std::string &name);
    // 叫一條收工並移出登記處。找不到回 false。
    bool stop(const std::string &name);
    std::vector<LoopInfo> snapshot();
    // daemon 收工時把全部叫回來。
    void shutdown_all();

  private:
    std::mutex mutex_;
    // ★ 用 map<string, unique_ptr> 而不是 map<string, Loop>：Loop 不可移動
    //   （裡面有 mutex 和 thread），而且外面拿到的 Loop& 必須在別人新增／移除
    //   其他 loop 的時候仍然有效。
    std::map<std::string, std::unique_ptr<Loop>> loops_;
};

Registry &registry();

}  // namespace aoslisp
