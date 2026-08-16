#include "loop.hpp"

#include "vm.hpp"

namespace aoslisp {

Loop::Loop(std::string name) : name_(std::move(name)) {
    thread_ = std::thread(&Loop::body, this);
}

Loop::~Loop() {
    request_stop();
    join();
}

bool Loop::submit(const JobPtr &job) {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closed_) return false;
        queue_.push_back(job);
    }
    cv_.notify_one();
    return true;
}

void Loop::request_stop() {
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closed_) return;
        closed_ = true;
        queue_.push_back(nullptr);  // 哨兵：排在它前面的照做完
    }
    cv_.notify_all();
}

void Loop::join() {
    if (thread_.joinable()) thread_.join();
}

unsigned long long Loop::handled() {
    std::lock_guard<std::mutex> guard(mutex_);
    return handled_;
}

std::size_t Loop::depth() {
    std::lock_guard<std::mutex> guard(mutex_);
    // 哨兵不算在待辦裡。
    std::size_t n = queue_.size();
    if (closed_ && n > 0) --n;
    return n;
}

// ★ 這是唯一碰 Vm 的地方，而且 Vm 是在**這條執行緒的堆疊上**建立與解構的。
//   Janet 的 VM 狀態是 thread-local，在 A 執行緒 init、B 執行緒 deinit 是 UB，
//   所以 Vm 不能是成員變數，也不能由建構 Loop 的那條執行緒去造。
void Loop::body() {
    Vm vm;

    for (;;) {
        JobPtr job;
        {
            std::unique_lock<std::mutex> guard(mutex_);
            cv_.wait(guard, [this] { return !queue_.empty(); });
            job = queue_.front();
            queue_.pop_front();
        }
        if (!job) break;  // 哨兵

        vm.run(*job);  // 不丟例外：Vm::run 是 noexcept，錯誤都變成 job 上的資料

        {
            std::lock_guard<std::mutex> guard(mutex_);
            ++handled_;
        }
        job->finish();
    }

    // 收工之後可能還有人在哨兵後面 submit 失敗前擠進來的 job（submit 有鎖 +
    // closed_ 檢查，所以其實不會有）——保險起見還是把等的人放掉，
    // 不然那條連線執行緒會永遠卡在 wait() 上。
    std::deque<JobPtr> leftover;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        leftover.swap(queue_);
    }
    for (const JobPtr &job : leftover) {
        if (!job) continue;
        job->abandoned = true;
        job->err = "loop 已收工，這個 job 沒有被執行\n";
        job->status = 1;
        job->finish();
    }
}

// ── 登記處 ──────────────────────────────────────────────────────────

Loop &Registry::acquire(const std::string &name) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = loops_.find(name);
    if (it == loops_.end()) {
        it = loops_.emplace(name, std::make_unique<Loop>(name)).first;
    }
    return *it->second;
}

Loop *Registry::find(const std::string &name) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = loops_.find(name);
    return it == loops_.end() ? nullptr : it->second.get();
}

bool Registry::stop(const std::string &name) {
    std::unique_ptr<Loop> taken;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = loops_.find(name);
        if (it == loops_.end()) return false;
        taken = std::move(it->second);
        loops_.erase(it);
    }
    // ★ 在鎖外面解構。~Loop 會 join，而那條 loop 可能正在跑一個很久的任務；
    //   抓著登記處的鎖去等它，會讓其他連線連「列出有哪些 loop」都卡住。
    taken.reset();
    return true;
}

std::vector<LoopInfo> Registry::snapshot() {
    std::vector<LoopInfo> out;
    std::lock_guard<std::mutex> guard(mutex_);
    out.reserve(loops_.size());
    for (const auto &entry : loops_) {
        out.push_back(LoopInfo{entry.first, entry.second->handled(),
                               entry.second->depth()});
    }
    return out;
}

void Registry::shutdown_all() {
    std::map<std::string, std::unique_ptr<Loop>> taken;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        taken.swap(loops_);
    }
    // 先全部叫收工再一個一個等，而不是「叫一個等一個」——後者的總時間是每條
    // loop 手上那個任務的**總和**，前者是最大值。
    for (const auto &entry : taken) entry.second->request_stop();
    taken.clear();
}

Registry &registry() {
    // ★ function-local static：C++11 起保證執行緒安全的初始化，而且比檔案層級的
    //   全域變數好——後者的建構順序跟 dlopen 的時機混在一起會很難推理。
    static Registry instance;
    return instance;
}

}  // namespace aoslisp
