// aos-simple 的核心：struct、queue、loop。
//
// 這裡一個子行程都沒有起——那正是把 exec 關在 Executor 後面換到的東西。

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../src/priority_queue.hpp"
#include "../src/queue.hpp"
#include "../src/loop.hpp"
#include "../src/router.hpp"
#include "../src/tracker.hpp"
#include "check.hpp"

using namespace aossimple;

namespace {

// 一個形狀正確的 Call，測試再各自改壞其中一項。
Call good() {
    Call c;
    c.argv = {"/bin/echo", "hi"};
    c.stdin_path = "/tmp/aos-simple/in";
    c.stdout_path = "/tmp/aos-simple/out";
    c.stderr_path = "/tmp/aos-simple/err";
    c.exit_path = "/tmp/aos-simple/exit";
    c.cwd = "/tmp";
    return c;
}

// 記下被交到手上的每一個 Call，並回一個講好的結論。
class Recording : public Executor {
  public:
    explicit Recording(Outcome reply) : reply_(std::move(reply)) {}
    Outcome run(const Call &call) override {
        seen.push_back(call);
        return reply_;
    }
    std::vector<Call> seen;

  private:
    Outcome reply_;
};

class Throwing : public Executor {
  public:
    Outcome run(const Call &) override { throw std::runtime_error("執行者壞了"); }
};

void test_validate() {
    check::section("驗證：純函式，不碰檔案系統");

    check::ok(bool(validate(good())), "形狀正確就過");

    // ★ 路徑指向不存在的東西也照過——那要碰檔案系統，不歸驗證管
    Call missing = good();
    missing.cwd = "/no/such/dir/at/all";
    check::ok(bool(validate(missing)), "★ 不存在的路徑不歸形狀驗證管");

    Call empty_argv = good();
    empty_argv.argv.clear();
    check::ok(!validate(empty_argv), "空 argv 拒絕");

    // ★ argv[0] 不必是路徑：可以是 PATH 裡查得到的名字，也可以是純粹的路由鍵
    //   （像 llm-ask，那種根本不會被 exec）。「argv[0] 該長什麼樣」是各個目的地
    //   自己的事，不是共通形狀的事。
    Call bare = good();
    bare.argv = {"echo"};
    check::ok(bool(validate(bare)), "★ argv[0] 是裸名字可以（走 PATH 查找）");

    Call logical = good();
    logical.argv = {"llm-ask", "問題"};
    check::ok(bool(validate(logical)), "★ argv[0] 是路由用的邏輯名字也可以");

    Call empty_argv0 = good();
    empty_argv0.argv = {""};
    check::ok(!validate(empty_argv0), "但 argv[0] 空字串拒絕");

    Call nul_argv = good();
    nul_argv.argv = {"/bin/echo", std::string("a\0b", 3)};
    check::ok(!validate(nul_argv), "★ argv 含 NUL 拒絕——execve 帶不過去");

    // 五個路徑欄位，每一個都要擋相對路徑和空字串
    struct Field {
        std::string Call::*member;
        const char *name;
    };
    const Field fields[] = {
        {&Call::stdin_path, "stdin"}, {&Call::stdout_path, "stdout"},
        {&Call::stderr_path, "stderr"}, {&Call::exit_path, "exit"},
        {&Call::cwd, "cwd"},
    };
    for (const Field &f : fields) {
        Call rel = good();
        rel.*(f.member) = "relative/path";
        check::ok(!validate(rel), std::string{f.name} + " 相對路徑拒絕");

        Call blank = good();
        blank.*(f.member) = "";
        check::ok(!validate(blank), std::string{f.name} + " 空字串拒絕");
    }

    // user：唯一可選的，而且不是路徑
    Call no_user = good();
    check::ok(!no_user.user.has_value(), "user 預設就是沒有");
    check::ok(bool(validate(no_user)), "沒給 user 也過");

    Call with_user = good();
    with_user.user = "agent-7";
    check::ok(bool(validate(with_user)), "★ user 不必是路徑");

    Call blank_user = good();
    blank_user.user = "";
    check::ok(bool(validate(blank_user)),
              "★ 空字串的 user 是合法的——「沒給」是 nullopt，兩件事分得開");

    Call nul_user = good();
    nul_user.user = std::string("a\0b", 3);
    check::ok(!validate(nul_user), "user 含 NUL 拒絕");

    // env：可選，給了就比照路徑
    Call no_env = good();
    check::ok(!no_env.env.has_value(), "env 預設就是沒有");
    check::ok(bool(validate(no_env)), "沒給 env 也過");

    Call with_env = good();
    with_env.env = "/etc/aos/profile";
    check::ok(bool(validate(with_env)), "給了絕對路徑就過");

    Call rel_env = good();
    rel_env.env = "profile";
    check::ok(!validate(rel_env), "★ env 相對路徑拒絕——它跟其他五個一樣是路徑");

    // 訊息要講得出是哪個欄位
    const Check why = validate(empty_argv0);
    check::ok(why.reason.find("argv[0]") != std::string::npos,
              "拒絕的理由指得出欄位");
}

void test_router() {
    check::section("★ 依 argv[0] 分流");

    Queue llm;
    Queue fallback;
    Router router;
    router.route("llm-ask", llm);
    router.fallback(fallback);

    // 命中邏輯名字 -> 專屬的 queue
    Call ask = good();
    ask.argv = {"llm-ask", "今天天氣如何"};
    const Router::Decision d1 = router.dispatch(ask);
    check::ok(d1.result == Router::Result::Routed, "★ llm-ask 被路由走了");
    check::eq(d1.destination, std::string{"llm-ask"}, "說得出去了哪");
    check::eq(llm.depth(), std::size_t{1}, "進了 llm 的 queue");
    check::eq(fallback.depth(), std::size_t{0}, "沒有進 fallback");

    // 沒命中 -> fallback
    Call ls = good();
    ls.argv = {"/bin/ls"};
    const Router::Decision d2 = router.dispatch(ls);
    check::ok(d2.result == Router::Result::FellBack, "沒命中就走 fallback");
    check::eq(fallback.depth(), std::size_t{1}, "進了 fallback 的 queue");
    check::eq(llm.depth(), std::size_t{1}, "llm 的 queue 沒被動到");

    // ★ 整串精確比對，不取 basename
    Call other_path = good();
    other_path.argv = {"/opt/somewhere/llm-ask"};
    router.dispatch(other_path);
    check::eq(fallback.depth(), std::size_t{2},
              "★ /opt/…/llm-ask 不會自動命中——basename 相同不算數");
    check::eq(llm.depth(), std::size_t{1}, "llm 的 queue 還是 1");

    // 要它命中就明寫上去
    Queue llm2;
    Router r2;
    r2.route("llm-ask", llm2);
    r2.route("/usr/bin/llm-ask", llm2);
    Call abs_ask = good();
    abs_ask.argv = {"/usr/bin/llm-ask"};
    check::ok(r2.dispatch(abs_ask).result == Router::Result::Routed,
              "明寫上去就命中");
    check::eq(r2.names().size(), std::size_t{2}, "兩個名字都在");

    // 沒有 fallback 的時候，沒命中就是沒地方去——要講出來
    Router bare;
    Queue only;
    bare.route("llm-ask", only);
    Call stray = good();
    stray.argv = {"/bin/ls"};
    check::ok(bare.dispatch(stray).result == Router::Result::NoRoute,
              "★ 沒 fallback 又沒命中 => NoRoute，不安靜丟掉");
    check::eq(only.depth(), std::size_t{0}, "而且沒有亂塞進別人的 queue");

    // 目的地收工了也要講出來
    Queue closed;
    Router r3;
    r3.route("llm-ask", closed);
    closed.close();
    Call late = good();
    late.argv = {"llm-ask"};
    check::ok(r3.dispatch(late).result == Router::Result::Closed,
              "★ 目的地收工 => Closed，呼叫端才不會等一個不會被處理的呼叫");

    // 空 argv 沒有 argv[0] 可看：不分流，但也不當場爆掉（形狀是目的地的事）
    Queue empty_fb;
    Router r4;
    r4.fallback(empty_fb);
    Call no_argv = good();
    no_argv.argv.clear();
    check::ok(r4.dispatch(no_argv).result == Router::Result::FellBack,
              "空 argv 走 fallback，由目的地去拒絕它");
}

// llm-ask 那種 loop 會做的事：從 argv 裡撈出 --priority。
// ★ 這段**不在核心裡**——核心不認識 priority，它只收一個算優先值的函式。
long priority_from_argv(const Call &call) {
    for (std::size_t i = 0; i + 1 < call.argv.size(); ++i) {
        if (call.argv[i] == "--priority") return std::stol(call.argv[i + 1]);
    }
    return 0;
}

Call with_priority(const std::string &tag, long p) {
    Call c = good();
    c.argv = {"llm-ask", "--priority", std::to_string(p)};
    c.user = tag;
    return c;
}

void test_priority_queue() {
    check::section("★ PriorityQueue：排序政策由 loop 自己給");

    {
        PriorityQueue q{priority_from_argv};
        q.push(with_priority("low", 1));
        q.push(with_priority("high", 9));
        q.push(with_priority("mid", 5));

        check::eq(q.depth(), std::size_t{3}, "三個都排著");
        check::eq(q.take()->user.value(), std::string{"high"}, "★ 數字大的先做");
        check::eq(q.take()->user.value(), std::string{"mid"}, "再來中的");
        check::eq(q.take()->user.value(), std::string{"low"}, "最後小的");
    }

    // ★★ 同分一定要 FIFO，否則同優先權的順序不可重現、會有人餓死
    {
        PriorityQueue q{priority_from_argv};
        for (int i = 0; i < 8; ++i) q.push(with_priority(std::to_string(i), 5));
        bool in_order = true;
        for (int i = 0; i < 8; ++i) {
            if (q.take()->user.value() != std::to_string(i)) in_order = false;
        }
        check::ok(in_order, "★★ 同分嚴格照先來後到");
    }

    // 混合：同分之間 FIFO，不同分照優先權
    {
        PriorityQueue q{priority_from_argv};
        q.push(with_priority("a5", 5));
        q.push(with_priority("b9", 9));
        q.push(with_priority("c5", 5));
        q.push(with_priority("d9", 9));
        check::eq(q.take()->user.value(), std::string{"b9"}, "9 分先來的");
        check::eq(q.take()->user.value(), std::string{"d9"}, "9 分後來的");
        check::eq(q.take()->user.value(), std::string{"a5"}, "5 分先來的");
        check::eq(q.take()->user.value(), std::string{"c5"}, "5 分後來的");
    }

    // 不給函式 = 全部同分 = 退化成 FIFO
    {
        PriorityQueue q;
        q.push(with_priority("first", 1));
        q.push(with_priority("second", 99));
        check::eq(q.take()->user.value(), std::string{"first"},
                  "★ 沒給優先值函式就是純 FIFO（99 也不會插隊）");
    }

    // poll 不擋
    {
        PriorityQueue q{priority_from_argv};
        check::ok(!q.poll().has_value(), "空的 poll 立刻回");
        q.push(with_priority("x", 1));
        check::ok(q.poll().has_value(), "有東西就取得到");
    }

    // ★ 收工：排在前面的照做完
    {
        PriorityQueue q{priority_from_argv};
        q.push(with_priority("a", 1));
        q.push(with_priority("b", 2));
        q.close();
        check::ok(q.closed(), "關了");
        check::eq(q.depth(), std::size_t{2}, "★ close 不會丟掉已經排著的");
        check::eq(q.take()->user.value(), std::string{"b"}, "★ 而且還是照優先權出");
        check::ok(q.take().has_value(), "第二個也取得到");
        check::ok(!q.take().has_value(), "空了 => 收工");
        check::ok(!q.take().has_value(),
                  "★★ 再取一次還是收工，不會擋住——Pool 的 N 條 worker 靠這個");
        check::ok(!q.push(with_priority("late", 1)), "關了之後 push 回 false");
    }

    // ★★ N 條 worker 都要收得到收工廣播。
    //    PriorityQueue 的收工是**旗標**不是哨兵（哨兵會被優先值排到不知道哪去），
    //    所以這是跟 Queue 完全不同的一條路徑，要單獨守。
    {
        PriorityQueue q{priority_from_argv};
        constexpr int kWorkers = 6;
        std::atomic<int> finished{0};
        std::atomic<int> taken{0};
        std::vector<std::thread> workers;
        for (int i = 0; i < kWorkers; ++i) {
            workers.emplace_back([&] {
                while (q.take().has_value()) taken.fetch_add(1);
                finished.fetch_add(1);
            });
        }
        for (int i = 0; i < 50; ++i) q.push(with_priority("w", i));
        q.close();
        for (std::thread &t : workers) t.join();  // ★ 卡住就是這裡沒過

        check::eq(finished.load(), kWorkers, "★★ 六條 worker 全部收到收工廣播");
        check::eq(taken.load(), 50, "★ 而且 50 個一個都沒掉、也沒重複");
    }
}

// 文檔〈loop 之間怎麼傳〉那個 fan-out 模式的可執行版本。
class FanOut : public Executor {
  public:
    FanOut(Sink &downstream, int children) : down_(downstream), children_(children) {}

    Outcome run(const Call &call) override {
        for (int i = 0; i < children_; ++i) {
            Call child = call;
            child.argv = {"child-work", std::to_string(i)};
            // ★★ 每個新 Call 一定要有**自己的**證據路徑。共用的話兩個人會搶著
            //    寫同一個 exit 檔，「exit 檔在 = 這一次的結論」就不成立了。
            const std::string tag = ".child" + std::to_string(i);
            child.stdout_path = call.stdout_path + tag;
            child.stderr_path = call.stderr_path + tag;
            child.exit_path = call.exit_path + tag;
            down_.push(child);
        }
        // 自己這通呼叫的結論照常給。★ 交出去的是**新的** Call，不是這一個。
        return Outcome::exited(0);
    }

  private:
    Sink &down_;
    int children_;
};

void test_fan_out() {
    check::section("★ loop 之間傳遞：fan-out");

    Queue upstream;
    Queue downstream;
    FanOut fan{downstream, 3};
    Loop loop{upstream, fan};

    Call c = good();
    c.exit_path = "/tmp/aos-simple/parent-exit";
    upstream.push(c);
    loop.drain();

    check::eq(downstream.depth(), std::size_t{3}, "★ 一個進來，三個交下去");

    std::vector<std::string> exits;
    while (auto child = downstream.poll()) exits.push_back(child->exit_path);
    check::eq(exits.size(), std::size_t{3}, "三個都拿得到");
    check::eq(exits[0], std::string{"/tmp/aos-simple/parent-exit.child0"},
              "★ 每個子 Call 有自己的 exit 路徑");
    check::ok(exits[0] != exits[1] && exits[1] != exits[2],
              "★★ 三個互不相同——不可以有兩個人寫同一個 exit 檔");
    check::eq(loop.stats().handled, 1ull, "上游自己這通照常算一個");
}

void test_tracker() {
    check::section("★★ WorkTracker：收工前先確認真的沒事了");

    {
        WorkTracker t;
        check::eq(t.in_flight(), 0ull, "一開始是 0");
        t.entered();
        t.entered();
        check::eq(t.in_flight(), 2ull, "進來兩件");
        t.left();
        check::eq(t.in_flight(), 1ull, "走了一件");
        t.left();
        check::eq(t.in_flight(), 0ull, "空了");
        t.wait_idle();  // ★ 已經是 0，不可以擋住
        check::ok(true, "★ 已經歸零時 wait_idle 立刻回來");

        t.left();  // 防呆：不該發生，但不可以繞回天文數字
        check::eq(t.in_flight(), 0ull, "★ 多減不會變成負的");
    }

    // TrackedSink：push 成功才算數
    {
        WorkTracker t;
        Queue q;
        TrackedSink sink{q, t};
        check::ok(sink.push(good()), "push 成功");
        check::eq(t.in_flight(), 1ull, "★ 算進去了");

        q.close();
        check::ok(!sink.push(good()), "關了之後 push 失敗");
        check::eq(t.in_flight(), 1ull,
                  "★★ 沒收下的不算——不然計數永遠歸不了零，收工就卡死");
    }

    // wrap：先跑使用者的 observer，再減
    {
        WorkTracker t;
        std::vector<unsigned long long> seen;
        auto wrapped = t.wrap([&](const Call &, const Outcome &) {
            seen.push_back(t.in_flight());  // observer 裡看到的計數
        });
        t.entered();
        wrapped(good(), Outcome::exited(0));
        check::eq(seen.size(), std::size_t{1}, "observer 被叫了");
        check::eq(seen[0], 1ull, "★ observer 跑的時候還沒減");
        check::eq(t.in_flight(), 0ull, "跑完才減");

        auto bare = t.wrap(nullptr);
        t.entered();
        bare(good(), Outcome::exited(0));
        check::eq(t.in_flight(), 0ull, "沒給 inner 也能用");
    }

    // ★★★ 本節重點：處理途中生出新工作，計數不可以假性歸零。
    //
    //   A 做完 -> 推一個 B；B 做完 -> 什麼都不推。
    //   naive 的「兩個 queue 都空了就收工」會在 A 剛做完的那一瞬間誤判。
    {
        WorkTracker t;
        Queue qa, qb;
        TrackedSink sa{qa, t}, sb{qb, t};

        // A 的 executor：做事的時候往 B 推一個
        class Forward : public Executor {
          public:
            explicit Forward(Sink *to) : to_(to) {}
            Outcome run(const Call &call) override {
                if (to_) to_->push(call);
                return Outcome::exited(0);
            }
            Sink *to_;
        };
        Forward fa{&sb};
        Forward fb{nullptr};

        Loop la{qa, fa, t.wrap(nullptr)};
        Loop lb{qb, fb, t.wrap(nullptr)};

        sa.push(good());
        check::eq(t.in_flight(), 1ull, "一件事進來了");

        la.drain();  // A 做完，順手推了一個給 B
        check::eq(qa.depth(), std::size_t{0}, "A 的 queue 空了");
        check::eq(t.in_flight(), 1ull,
                  "★★★ 但計數還是 1——那件事只是移到 B 那邊，不是消失了");

        lb.drain();
        check::eq(t.in_flight(), 0ull, "★ B 也做完了才真的歸零");
        t.wait_idle();
        check::ok(true, "★ 這時候 wait_idle 才回得來");
    }

    // 多執行緒下也對
    {
        WorkTracker t;
        constexpr int kThreads = 8, kEach = 200;
        std::vector<std::thread> ts;
        for (int i = 0; i < kThreads; ++i) {
            ts.emplace_back([&] {
                for (int k = 0; k < kEach; ++k) {
                    t.entered();
                    t.left();
                }
            });
        }
        t.entered();  // 讓它在整段期間都不是 0
        for (std::thread &th : ts) th.join();
        check::eq(t.in_flight(), 1ull, "★ 8 條執行緒各 200 次進出，數目正確");
        t.left();

        // wait_idle 真的會被叫醒
        t.entered();
        std::thread releaser{[&] {
            std::this_thread::sleep_for(std::chrono::milliseconds{30});
            t.left();
        }};
        t.wait_idle();  // ★ 卡住就是這裡沒過
        releaser.join();
        check::ok(true, "★ wait_idle 擋住之後被 left() 叫醒");
    }
}

void test_queue() {
    check::section("佇列");

    Queue q;
    check::eq(q.depth(), std::size_t{0}, "新的是空的");
    check::ok(!q.closed(), "新的沒關");
    check::ok(!q.poll().has_value(), "空的 poll 立刻回，不擋");

    Call a = good();
    a.user = "first";
    Call b = good();
    b.user = "second";
    check::ok(q.push(a), "push 回 true");
    q.push(b);
    check::eq(q.depth(), std::size_t{2}, "depth 數得對");

    check::eq(q.take()->user.value(), std::string{"first"}, "FIFO：先進先出");
    check::eq(q.depth(), std::size_t{1}, "take 之後 depth 減一");
    check::eq(q.take()->user.value(), std::string{"second"}, "第二個");

    // ★ close 之後排在前面的還取得到
    Queue q2;
    q2.push(good());
    q2.push(good());
    q2.close();
    check::ok(q2.closed(), "關了");
    check::eq(q2.depth(), std::size_t{2}, "★ close 不會丟掉已經排著的");
    check::ok(q2.take().has_value(), "★ close 之後前面排的照取得到");
    check::ok(q2.take().has_value(), "★ 第二個也是");
    check::ok(!q2.take().has_value(), "取到哨兵 => 收工");
    check::ok(!q2.take().has_value(),
              "★ 再取一次還是「收工」，不會永遠擋住");
    check::ok(!q2.push(good()), "關了之後 push 回 false");

    Queue q3;
    q3.close();
    check::eq(q3.depth(), std::size_t{0}, "★ 只有哨兵時 depth 是 0");
    check::ok(!q3.poll().has_value(), "poll 不會把哨兵當資料取出來");

    Queue q4;
    q4.close();
    q4.close();
    check::ok(!q4.take().has_value(), "重複 close 只放一個哨兵");
}

void test_loop() {
    check::section("loop");

    {
        Queue q;
        Recording exec{Outcome::exited(0)};
        std::vector<Outcome> results;
        Loop loop{q, exec, [&](const Call &, const Outcome &o) { results.push_back(o); }};

        q.push(good());
        q.push(good());
        check::eq(loop.drain(), std::size_t{2}, "drain 把排著的都做完");
        check::eq(exec.seen.size(), std::size_t{2}, "兩個都交到執行者手上");
        check::eq(loop.stats().handled, 2ull, "handled 數得對");
        check::eq(loop.stats().rejected, 0ull, "沒有被拒絕的");
        check::eq(results.size(), std::size_t{2}, "observer 每個都叫");
        check::ok(results[0].status == Status::Exited, "結論傳回來了");
        check::ok(!loop.step(), "空的 step 回 false，不擋");
    }

    {
        // ★ loop 自己不驗證——那是 Executor 的事（因為 Rejected 也要寫 exit 檔，
        //   而寫檔是執行者的職責）。loop 只負責把 Rejected 這個結論算進統計。
        Queue q;
        Recording exec{Outcome::rejected("形狀不對")};
        std::vector<Outcome> results;
        Loop loop{q, exec, [&](const Call &, const Outcome &o) { results.push_back(o); }};

        Call bad = good();
        bad.argv = {"echo"};  // 相對路徑
        q.push(bad);
        loop.drain();

        check::eq(exec.seen.size(), std::size_t{1},
                  "★ 形狀不對的照樣交到執行者手上，loop 不預判");
        check::ok(results[0].status == Status::Rejected, "結論是 Rejected");
        check::eq(loop.stats().rejected, 1ull, "loop 有把它算進 rejected");
    }

    {
        // ★ 執行者丟例外炸不掉 loop，而且結論是 Unknown 不是失敗
        Queue q;
        Throwing exec;
        std::vector<Outcome> results;
        Loop loop{q, exec, [&](const Call &, const Outcome &o) { results.push_back(o); }};

        q.push(good());
        q.push(good());
        loop.drain();  // ★ 沒炸就是重點

        check::eq(results.size(), std::size_t{2}, "兩個都有結論");
        check::ok(results[0].status == Status::Unknown,
                  "★ 執行者丟例外 => Unknown，不是 LaunchError");
        check::ok(results[0].reason.find("例外") != std::string::npos,
                  "說得出是例外");
    }

    {
        // 預設執行者：誠實地說不知道
        Queue q;
        DefaultExecutor exec;
        std::vector<Outcome> results;
        Loop loop{q, exec, [&](const Call &, const Outcome &o) { results.push_back(o); }};
        q.push(good());
        loop.drain();
        check::ok(results[0].status == Status::Unknown,
                  "★ 沒接執行者的時候回 Unknown，不假裝成功也不假裝失敗");
    }

    {
        // run() 靠哨兵回來，而且排在前面的都做完
        Queue q;
        Recording exec{Outcome::exited(0)};
        Loop loop{q, exec};
        q.push(good());
        q.push(good());
        q.close();
        loop.run();  // ★ 不會永遠擋住
        check::eq(loop.stats().handled, 2ull, "★ close 之前排的都做完了");
    }

    {
        // Call 原樣傳到執行者手上，沒有被改
        Queue q;
        Recording exec{Outcome::exited(0)};
        Loop loop{q, exec};
        Call c = good();
        c.argv = {"/bin/sh", "-c", "echo 中文"};
        c.user = "agent-7";
        q.push(c);
        loop.drain();
        check::eq(exec.seen[0].argv.size(), std::size_t{3}, "argv 完整");
        check::eq(exec.seen[0].argv[2], std::string{"echo 中文"},
                  "★ 含空白與非 ASCII 的參數原樣過去，沒有被重新拼接");
        check::eq(exec.seen[0].user.value(), std::string{"agent-7"}, "user 過去了");
        check::eq(exec.seen[0].exit_path, c.exit_path, "exit 路徑過去了");
    }
}

void test_concurrent_producers() {
    check::section("多個生產者");

    Queue q;
    Recording exec{Outcome::exited(0)};
    Loop loop{q, exec};

    constexpr int kThreads = 8;
    constexpr int kEach = 50;
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&q, t] {
            for (int i = 0; i < kEach; ++i) {
                Call c = good();
                c.user = std::to_string(t) + "-" + std::to_string(i);
                q.push(c);
            }
        });
    }

    // 消費者跟生產者同時跑。
    std::thread consumer{[&loop] { loop.run(); }};

    for (std::thread &t : producers) t.join();
    q.close();
    consumer.join();

    check::eq(loop.stats().handled,
              static_cast<unsigned long long>(kThreads * kEach),
              "★ 8 條生產者各推 50 個，一個都沒掉、也沒重複");
    check::eq(exec.seen.size(), static_cast<std::size_t>(kThreads * kEach),
              "執行者收到的數目一樣");
    check::eq(q.depth(), std::size_t{0}, "佇列清空了");
}

}  // namespace

int main() {
    test_validate();
    test_router();
    test_priority_queue();
    test_tracker();
    test_fan_out();
    test_queue();
    test_loop();
    test_concurrent_producers();
    return check::report();
}
