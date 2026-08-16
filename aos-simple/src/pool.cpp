#include "pool.hpp"

namespace aossimple {

// 包住真正的 executor：進去前加一、出來後減一，順便記最高點。
class Pool::Tracking : public Executor {
  public:
    Tracking(Executor &inner, std::atomic<std::size_t> &in_flight,
             std::atomic<std::size_t> &peak)
        : inner_(inner), in_flight_(in_flight), peak_(peak) {}

    Outcome run(const Call &call) override {
        const std::size_t now = in_flight_.fetch_add(1) + 1;
        // ★ compare_exchange 迴圈，不是 `if (now > peak) peak = now`——
        //   後者在兩條 worker 同時進來時會互相蓋掉，記到偏低的值。
        std::size_t seen = peak_.load();
        while (now > seen && !peak_.compare_exchange_weak(seen, now)) {
            /* seen 被更新成當前值，再比一次 */
        }

        // ⚠ inner 丟例外的話這裡的減一會被跳過，之後的計數就永遠偏高。
        //   Loop 會接住例外，但那是在**這一層外面**接的，所以要自己顧。
        try {
            const Outcome outcome = inner_.run(call);
            in_flight_.fetch_sub(1);
            return outcome;
        } catch (...) {
            in_flight_.fetch_sub(1);
            throw;  // 還是讓 Loop 去把它變成 Unknown
        }
    }

  private:
    Executor &inner_;
    std::atomic<std::size_t> &in_flight_;
    std::atomic<std::size_t> &peak_;
};

Pool::Pool(Source &queue, Executor &executor, std::size_t workers,
           Loop::Observer observer)
    : queue_(queue), observer_(std::move(observer)) {
    if (workers < 1) workers = 1;
    tracking_ = std::make_unique<Tracking>(executor, in_flight_, peak_);

    // observer 由這裡上鎖串起來，寫 observer 的人就不必想併發的事。
    Loop::Observer guarded;
    if (observer_) {
        guarded = [this](const Call &call, const Outcome &outcome) {
            std::lock_guard<std::mutex> lock(observer_mutex_);
            observer_(call, outcome);
        };
    }

    loops_.reserve(workers);
    threads_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
        // ★ 一條 worker 一個 Loop：Loop 的 stats 不是 atomic 的，共用一個就會race。
        //   Queue 是共用的（它本來就多執行緒安全），Executor 也是共用的。
        loops_.push_back(std::make_unique<Loop>(queue_, *tracking_, guarded));
        Loop *loop = loops_.back().get();
        threads_.emplace_back([loop] { loop->run(); });
    }
}

Pool::~Pool() {
    // ⚠ 這裡**不會**自己 close queue。誰開的誰關——Pool 不知道還有沒有別人
    //   要往那個 queue 裡放東西。沒 close 就解構的話這裡會擋住，那是對的：
    //   安靜地丟下還在跑的 worker 才是災難。
    join();
}

void Pool::join() {
    if (joined_) return;
    for (std::thread &t : threads_) {
        if (t.joinable()) t.join();
    }
    joined_ = true;
}

Stats Pool::stats() const {
    Stats total;
    for (const std::unique_ptr<Loop> &loop : loops_) {
        const Stats one = loop->stats();
        total.handled += one.handled;
        total.rejected += one.rejected;
    }
    return total;
}

}  // namespace aossimple
