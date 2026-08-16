#pragma once
// job.hpp — 一次求值請求。連線執行緒和 loop 執行緒之間**只透過這個結構交換位元組**。
//
// ★ 為什麼是位元組，不是 Janet 值
//
// Janet 的 VM 狀態是 thread-local（janet.h 把 JANET_THREAD_LOCAL 定成 __thread）。
// 一條 loop 一個 janet_init()，所以**只有那條 loop 自己的執行緒可以碰它的 VM**。
// 連線執行緒碰到任何 janet_* 都是未定義行為，而且會是那種很難查的壞法。
//
// 把交換介面收斂成「進去一段原始碼、回來兩段輸出加一個 exit status」，
// 這條規矩就從「要記得遵守」變成「想違反也沒有型別可用」。

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace aoslisp {

struct Job {
    // 進去：要求值的原始碼（可以有多個 form）。
    std::string source;

    // 回來：由 loop 執行緒填，填完才 notify。
    std::string out;
    std::string err;
    int status = 0;

    // loop 收工時還沒做的 job 會被標成這個，讓等的人不會永遠等下去。
    bool abandoned = false;

    void finish() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            done_ = true;
        }
        cv_.notify_all();
    }

    void wait() {
        std::unique_lock<std::mutex> guard(mutex_);
        cv_.wait(guard, [this] { return done_; });
    }

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
};

using JobPtr = std::shared_ptr<Job>;

}  // namespace aoslisp
