// main.cpp — 把所有零件接起來，變成一支跑得起來的程式。
//
//     aos-simple < calls.ndjson
//     echo '{"argv":["exec","/bin/echo","hi"], …}' | aos-simple
//
// ★ 這個檔**只有接線**，沒有任何邏輯。所有邏輯都在 src/ 與 ext/ 裡，
//   而且都各自有測試。要換掉輸入來源（socket、監看目錄）就只動這裡。
//
// ── 這台機器目前長這樣 ──────────────────────────────────────────────
//
//     stdin (NDJSON)  或  Unix socket（AOS_LISTEN=1）
//          │  ext/input：一行一個 JSON -> Call
//          ▼
//       Router                 看 argv[0]
//        ├── "exec"  ──▶ exec_queue  ──▶ Pool(4)  ──▶ ExecExecutor
//        └── "agent" ──▶ agent_queue ──▶ Pool(1)  ──▶ AgentExecutor
//                                                        │ 派 child
//                                                        └──▶ exec_queue
//                            ◀── continuation_observer ──┘（child 做完叫醒 Round）
//
// ★ **沒有 fallback。** 不走 exec／agent 的請求一律 NoRoute。
//   「最初入口預設關閉，想執行就得走 exec 這條」。

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "../ext/agent/agent.hpp"
#include "../ext/input/ndjson.hpp"
#include "../ext/input/socket.hpp"
#include "../src/exec.hpp"
#include "../src/pool.hpp"
#include "../src/queue.hpp"
#include "../src/router.hpp"
#include "../src/tracker.hpp"

namespace {

// 讓信號處理常式叫得到 server。★ 只有 stop() 是 async-signal-safe 的
//   （它只寫一個 byte 進 self-pipe）。
std::atomic<aosinput::SocketServer *> g_server{nullptr};

extern "C" void on_signal(int) {
    aosinput::SocketServer *server = g_server.load();
    if (server != nullptr) server->stop();
}

// 這一版的 controller 只是個佔位：它什麼都不決定，直接把 Round 收掉。
// ★ 真的 Agent 智慧要換的就是這個類別，其他一行都不用動。
//   （怎麼寫見 ext/agent/README.md。）
class NoopController : public aosagent::Controller {
  public:
    Decision decide(const aosagent::Round &, const aosagent::ChildResult &) override {
        return Decision::finish(0);
    }
};

long long env_number(const char *name, long long fallback) {
    const char *raw = std::getenv(name);
    if (raw == nullptr) return fallback;
    char *end = nullptr;
    const long long value = std::strtoll(raw, &end, 10);
    if (end == raw || value < 0) return fallback;
    return value;
}

std::size_t env_size(const char *name, std::size_t fallback) {
    const char *raw = std::getenv(name);
    if (raw == nullptr) return fallback;
    const long value = std::strtol(raw, nullptr, 10);
    if (value < 1) return fallback;
    return static_cast<std::size_t>(value);
}

}  // namespace

int main() {
    using namespace aossimple;

    const std::size_t exec_workers = env_size("AOS_EXEC_WORKERS", kDefaultExecWorkers);

    Queue exec_queue;
    Queue agent_queue;

    // ★★ 數「系統裡還有幾件事」。沒有它就收不了工：agent 派出去的 child
    //    做完會想推一個 tick 回來，那件事在「兩個 queue 都空了」的瞬間
    //    可能正好在半空中。見 src/tracker.hpp。
    WorkTracker tracker;
    TrackedSink tracked_exec{exec_queue, tracker};
    TrackedSink tracked_agent{agent_queue, tracker};

    // ── exec 這條 ──
    //
    // ★ 逾時與輸出上限的**機制**在 ExecExecutor 裡，**政策**在這裡決定。
    //   預設都是 0（不限）：一個通用的執行器不該替使用者猜「多久算太久」，
    //   猜錯會砍掉合法的長工作。要限制就設環境變數；
    //   之後每條 loop 可以有自己的一套（llm-ask 那種會自己解析 --wait-timeout
    //   並壓上自己的天花板，見 docs/TODO.md）。
    const ExecOptions exec_limits = [] {
        ExecOptions o;
        o.timeout = std::chrono::milliseconds{env_number("AOS_EXEC_TIMEOUT_MS", 0)};
        o.grace = std::chrono::milliseconds{env_number("AOS_EXEC_GRACE_MS", 5000)};
        o.max_output_bytes = env_number("AOS_EXEC_MAX_OUTPUT", 0);
        return o;
    }();
    ExecExecutor raw_exec{[exec_limits](const Call &) { return exec_limits; }};
    DropArgv0Executor gated_exec{raw_exec};  // 把 "exec" 這個路由鍵吃掉

    // ── agent 這條 ──
    NoopController controller;
    // ★ agent 派 child 也要走 tracked sink，否則它派出去的工作沒被數到。
    aosagent::AgentExecutor agent_executor{controller, tracked_exec};

    // child 做完就把它的 Round 叫醒。★ exec loop 完全不認識 agent，
    //   它只是照約定叫自己的 observer。
    Pool exec_pool{exec_queue, gated_exec, exec_workers,
                   tracker.wrap(aosagent::continuation_observer(tracked_agent))};
    Pool agent_pool{agent_queue, agent_executor, 1, tracker.wrap(nullptr)};

    Router router;
    // ★ route 到 tracked sink，不是直接到 queue——否則外面進來的工作不會被數到。
    router.route("exec", tracked_exec);
    router.route(aosagent::kAgentKey, tracked_agent);
    // 刻意不設 fallback。

    std::atomic<unsigned long long> no_route{0};
    std::atomic<unsigned long long> closed{0};

    // 兩條輸入來源共用同一個 dispatch 決策。★ 這是「收下就回」換到的東西：
    //   socket 那條跟 stdin 那條做的事一模一樣，沒有任何從 Outcome 回到連線的路。
    auto route_one = [&](Call call) -> aosinput::Ack {
        const Router::Decision decision = router.dispatch(call);
        switch (decision.result) {
            case Router::Result::Routed:
            case Router::Result::FellBack:
                return aosinput::Ack::accepted();
            case Router::Result::NoRoute:
                // ★ 不可以安靜忽略：沒有人會處理它，等它的人會永遠等。
                no_route.fetch_add(1);
                return aosinput::Ack::refused(
                    "沒有這條路由：" +
                    (call.argv.empty() ? std::string{"(空 argv)"} : call.argv[0]));
            case Router::Result::Closed:
                closed.fetch_add(1);
                return aosinput::Ack::refused("目的地已收工");
        }
        return aosinput::Ack::refused("路由結果不明");
    };

    aosinput::ReadStats stats;
    if (std::getenv("AOS_LISTEN") != nullptr) {
        // ── socket：收下就回 ──
        aosinput::SocketServer server{aosinput::default_socket_path(), route_one};
        if (!server.listen()) {
            std::fprintf(stderr, "起不來：%s\n", server.error().c_str());
            return 2;
        }
        g_server.store(&server);
        ::signal(SIGINT, on_signal);
        ::signal(SIGTERM, on_signal);
        std::fprintf(stderr, "聽在 %s（Ctrl-C 收工）\n", server.path().c_str());

        server.serve();

        g_server.store(nullptr);
        stats.accepted = server.accepted();
        stats.rejected = server.refused();
        std::fprintf(stderr, "服務過 %llu 條連線\n", server.connections());
    } else {
        // ── stdin：一行一個 JSON ──
        stats = aosinput::read_ndjson(
            std::cin,
            [&](Call call) {
                const aosinput::Ack ack = route_one(std::move(call));
                if (!ack.ok) std::fprintf(stderr, "%s\n", ack.error.c_str());
            },
            [](unsigned long long line, const std::string &why) {
                std::fprintf(stderr, "第 %llu 行：%s\n", line, why.c_str());
            });
    }

    // ── 收工 ──
    //
    // ★★ 第一步是**等系統真的靜下來**，不是急著關 queue。
    //    工作會在 loop 之間來回（agent 派 child、child 做完叫醒 Round），
    //    所以「queue 空了」不等於「沒事了」。等計數歸零才是。
    tracker.wait_idle();

    // 靜下來之後才關。順序照資料流：agent 先，因為它還可能往 exec 派東西
    //（雖然此刻已經確定沒有了，但把順序寫對，之後加新 loop 才不會踩到）。
    agent_queue.close();
    agent_pool.join();
    exec_queue.close();
    exec_pool.join();

    const Stats exec_stats = exec_pool.stats();
    const Stats agent_stats = agent_pool.stats();
    std::fprintf(stderr,
                 "讀進 %llu 行（略過 %llu、拒絕 %llu）；"
                 "exec 做了 %llu（拒絕 %llu），agent 做了 %llu\n",
                 stats.accepted, stats.blank, stats.rejected, exec_stats.handled,
                 exec_stats.rejected, agent_stats.handled);

    // 有任何一行被拒絕、或有請求沒地方去，就是非 0。
    // ★ 個別 Call 的成敗**不影響**這裡——那些在各自的 exit 檔裡。
    return (stats.rejected == 0 && no_route.load() == 0 && closed.load() == 0) ? 0 : 1;
}
