// agent 擴充。會真的起子行程、真的寫檔，所以跟 test_core 分開。

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../ext/agent/agent.hpp"
#include "../src/exec.hpp"
#include "../src/pool.hpp"
#include "../src/queue.hpp"
#include "../src/router.hpp"
#include "check.hpp"

using namespace aossimple;
using namespace aosagent;

namespace {

std::string root;
int seq = 0;

std::string slurp(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "<不存在>";
    std::ostringstream b;
    b << in.rdbuf();
    return b.str();
}

bool exists(const std::string &path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

std::string fresh_round() {
    const std::string dir = root + "/round" + std::to_string(++seq);
    ::mkdir(dir.c_str(), 0700);
    return dir;
}

// 一個假的 controller：跑 n 個 child（都是 /bin/echo），然後收工。
// ★ 它不打網路、不解析任何東西——真的 controller 也是同一個形狀，
//   只是「決定下一步」那裡換成看模型回覆。
class Scripted : public Controller {
  public:
    explicit Scripted(int steps) : steps_(steps) {}

    Decision decide(const Round &round, const ChildResult &last) override {
        ++calls;
        if (last.exists) {
            seen_codes.push_back(last.done ? last.code : -1);
            if (!last.done) return Decision::wait();  // 還不知道，等下一次
        }
        if (static_cast<int>(round.step) >= steps_) {
            return Decision::finish(finish_code_);
        }
        Call next;
        next.argv = {"exec", "/bin/echo", "step" + std::to_string(round.step + 1)};
        return Decision::dispatch(next);
    }

    void finish_with(int code) { finish_code_ = code; }

    int calls = 0;
    std::vector<int> seen_codes;

  private:
    int steps_;
    int finish_code_ = 0;
};

// 只被叫一次就記下 round 進度，用來測「先存進度再派工」。
class OneShot : public Controller {
  public:
    Decision decide(const Round &, const ChildResult &) override {
        Call next;
        next.argv = {"exec", "/bin/true"};
        return Decision::dispatch(next);
    }
};

void test_round_state() {
    check::section("Round 狀態存在磁碟上");

    const std::string dir = fresh_round();
    const Call tick = start_round(dir);

    check::ok(exists(dir + "/state"), "start_round 寫了 state");
    check::ok(exists(dir + "/steps"), "steps 目錄建好了");
    check::eq(tick.argv[0], std::string{"agent"}, "tick 的路由鍵");
    check::eq(tick.argv[1], dir, "tick 帶著 round 目錄");
    check::ok(bool(validate(tick)), "★ tick 自己也是一個合法的 Call");

    Round r;
    check::ok(load_round(dir, r), "讀得回來");
    check::eq(r.step, 0ull, "從第 0 步開始");
    check::ok(!r.finished, "還沒收工");

    r.step = 7;
    r.finished = true;
    r.code = 3;
    save_round(r);
    Round back;
    load_round(dir, back);
    check::eq(back.step, 7ull, "★ 進度存得回來（Round 會跨行程重開）");
    check::ok(back.finished, "finished 也是");
    check::eq(back.code, 3, "code 也是");
}

void test_one_tick_one_step() {
    check::section("★ 一次 tick 只走一小步，不阻塞");

    const std::string dir = fresh_round();
    Queue downstream;
    OneShot controller;
    AgentExecutor agent{controller, downstream};

    const Outcome o = agent.run(start_round(dir));
    check::ok(o.status == Status::Exited, "tick 跑完就返回");
    check::eq(downstream.depth(), std::size_t{1}, "★ 派了一個 child 出去");

    Round r;
    load_round(dir, r);
    check::eq(r.step, 1ull, "進度前進一步");

    const Call child = *downstream.poll();
    check::eq(child.stdout_path, dir + "/steps/1/stdout",
              "★ child 的證據路徑由 agent loop 填，在 steps/1/ 底下");
    check::eq(child.exit_path, dir + "/steps/1/exit", "exit 也是");
    check::ok(bool(validate(child)), "★ 派出去的 child 是合法的 Call");
    check::eq(child.user.value(), std::string{"agent:"} + dir,
              "★ user 標著是哪個 Round 派的——continuation 靠這個認人");
    check::eq(child.argv[0], std::string{"exec"},
              "★ argv[0] 還是路由鍵，會再被 router 分流一次");
}

// 手工做一個指向既有 round 的 tick。
// ⚠ 不能用 start_round()——那會把 state 重設回第 0 步。
Call tick_for(const std::string &dir) {
    Call t;
    t.argv = {"agent", dir};
    t.stdin_path = "/dev/null";
    t.stdout_path = dir + "/tick.out";
    t.stderr_path = dir + "/tick.err";
    t.exit_path = dir + "/tick.exit";
    t.cwd = dir;
    return t;
}

void test_child_result_is_evidence_only() {
    check::section("★★ exit 檔不在 = 還不知道，不是失敗");

    const std::string dir = fresh_round();
    Queue downstream;
    Scripted controller{3};
    AgentExecutor agent{controller, downstream};

    agent.run(start_round(dir));   // 派出 child 1
    check::eq(downstream.depth(), std::size_t{1}, "派了第一個");
    downstream.poll();             // 假裝有人拿走了，但**沒有真的跑**

    // child 1 的 exit 檔不存在。再 tick 一次——這是續跑機制可能會發生的事
    //（例如 child 回了 Unknown 也會叫醒 Round）。
    agent.run(tick_for(dir));

    check::eq(downstream.depth(), std::size_t{0},
              "★★ 上一個 child 還沒有 exit 檔 => Wait，**不會又派一個**");
    check::eq(controller.seen_codes.back(), -1,
              "★ controller 拿到的是 done=false，由它決定要等還是放棄");

    Round r;
    load_round(dir, r);
    check::eq(r.step, 1ull, "★ 進度停在第一步，沒有前進");
}

void test_continuation_observer() {
    check::section("★ 續跑：child 做完把 Round 叫醒");

    Queue agent_queue;
    auto wake = continuation_observer(agent_queue);

    const std::string dir = fresh_round();
    Call child;
    child.argv = {"/bin/true"};
    child.stdin_path = "/dev/null";
    child.stdout_path = dir + "/o";
    child.stderr_path = dir + "/e";
    child.exit_path = dir + "/x";
    child.cwd = dir;
    child.user = std::string{"agent:"} + dir;

    wake(child, Outcome::exited(0));
    check::eq(agent_queue.depth(), std::size_t{1}, "★ 推了一個 tick 回 agent queue");
    const Call tick = *agent_queue.poll();
    check::eq(tick.argv[1], dir, "tick 指向正確的 round");

    // 不是 agent 派的就不管
    Call stranger = child;
    stranger.user = "someone-else";
    wake(stranger, Outcome::exited(0));
    check::eq(agent_queue.depth(), std::size_t{0}, "★ 別人的 child 不會誤觸");

    Call anonymous = child;
    anonymous.user.reset();
    wake(anonymous, Outcome::exited(0));
    check::eq(agent_queue.depth(), std::size_t{0}, "沒有 user 的也不會");

    // ★ 失敗的 child 也要叫醒——「失敗怎麼辦」是 controller 的政策
    wake(child, Outcome::exited(7));
    check::eq(agent_queue.depth(), std::size_t{1}, "★ child 失敗一樣叫醒 Round");
    agent_queue.poll();
    wake(child, Outcome::unknown("不知道"));
    check::eq(agent_queue.depth(), std::size_t{1},
              "★ 連 Unknown 也叫醒，讓 controller 自己決定要不要等");
}

// ★★ 整條線：agent loop ＋ exec loop 接起來，跑真的子行程。
void test_end_to_end() {
    check::section("★★ 端到端：Round 驅動三個真的 child");

    Queue agent_queue;
    Queue exec_queue;

    Router router;
    router.route("agent", agent_queue);
    router.route("exec", exec_queue);

    Scripted controller{3};
    controller.finish_with(0);

    // agent 這條：一次一個就好，它只是做決定，很快
    AgentExecutor agent_executor{controller, exec_queue};
    Pool agent_pool{agent_queue, agent_executor, 1};

    // exec 這條：四併發，child 做完就把 Round 叫醒
    ExecExecutor raw;
    DropArgv0Executor gated{raw};
    Pool exec_pool{exec_queue, gated, 4, continuation_observer(agent_queue)};

    const std::string dir = fresh_round();
    router.dispatch(start_round(dir));

    // 等 Round 收工。★ 這裡輪詢是**測試**在等，不是 loop 在等——
    //   loop 之間從頭到尾沒有任何人阻塞等別人。
    bool done = false;
    for (int i = 0; i < 400 && !done; ++i) {
        if (exists(dir + "/exit")) done = true;
        else ::usleep(25 * 1000);
    }

    // 收工：照資料流的順序關
    agent_queue.close();
    agent_pool.join();
    exec_queue.close();
    exec_pool.join();

    check::ok(done, "★★ Round 自己走完並寫了 exit 檔");
    check::eq(slurp(dir + "/exit"), std::string{"0\n"}, "Round 的結論");

    Round r;
    load_round(dir, r);
    check::eq(r.step, 3ull, "★ 走了三步");
    check::ok(r.finished, "狀態標成收工");

    for (int n = 1; n <= 3; ++n) {
        const std::string s = dir + "/steps/" + std::to_string(n);
        check::eq(slurp(s + "/stdout"), "step" + std::to_string(n) + "\n",
                  "★ 第 " + std::to_string(n) + " 個 child 真的跑了");
        check::eq(slurp(s + "/exit"), std::string{"0\n"},
                  "而且留下自己的 exit 檔");
    }

    bool all_zero = true;
    for (int c : controller.seen_codes) {
        if (c != 0) all_zero = false;
    }
    check::ok(all_zero, "★ controller 每次都看到上一個 child 的真實結果");
}

void test_finished_round_is_idempotent() {
    check::section("重複的 tick 不會重做");

    const std::string dir = fresh_round();
    Queue downstream;
    Scripted controller{0};  // 0 步 = 第一次就收工
    AgentExecutor agent{controller, downstream};

    const Call tick = start_round(dir);
    const Outcome first = agent.run(tick);
    check::ok(first.status == Status::Exited, "第一次就收工");
    check::ok(exists(dir + "/exit"), "寫了 exit");

    const int calls_before = controller.calls;
    const Outcome again = agent.run(tick);
    check::ok(again.status == Status::Exited, "再 tick 一次也不報錯");
    check::eq(controller.calls, calls_before,
              "★ 已收工的 Round 不會再問 controller，也不會重做");
    check::eq(downstream.depth(), std::size_t{0}, "更不會再派工");
}

void test_bad_tick() {
    check::section("壞的 tick");

    Queue downstream;
    Scripted controller{1};
    AgentExecutor agent{controller, downstream};

    Call bad;
    bad.argv = {"agent"};  // 沒帶 round 目錄
    bad.stdin_path = "/dev/null";
    bad.stdout_path = "/dev/null";
    bad.stderr_path = "/dev/null";
    bad.exit_path = root + "/bad.exit";
    bad.cwd = root;
    check::ok(agent.run(bad).status == Status::Rejected, "沒帶目錄 => Rejected");

    Call missing = bad;
    missing.argv = {"agent", root + "/no-such-round"};
    check::ok(agent.run(missing).status == Status::Rejected,
              "round 目錄不存在 => Rejected");
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/aos-simple-agent-XXXXXX";
    const char *made = ::mkdtemp(tmpl);
    if (made == nullptr) {
        std::fprintf(stderr, "開不了暫存目錄\n");
        return 1;
    }
    root = made;

    test_round_state();
    test_one_tick_one_step();
    test_child_result_is_evidence_only();
    test_continuation_observer();
    test_end_to_end();
    test_finished_round_is_idempotent();
    test_bad_tick();

    const int result = check::report();
    if (result == 0) {
        const std::string cmd = "rm -rf '" + root + "'";
        if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "清不掉 %s\n", root.c_str());
    } else {
        std::fprintf(stderr, "現場留在 %s\n", root.c_str());
    }
    return result;
}
