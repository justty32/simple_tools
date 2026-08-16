#include "agent.hpp"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "../../src/exec.hpp"

namespace aosagent {
namespace {

bool exists(const std::string &path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

std::string slurp(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// state 檔就是幾行 key=value。刻意不用 JSON：這一層零相依，而且這個檔的欄位
// 少到用 JSON 只是多帶一個 parser。
std::string render_state(const Round &round) {
    std::string out;
    out += "step=" + std::to_string(round.step) + "\n";
    out += "finished=" + std::string(round.finished ? "1" : "0") + "\n";
    out += "code=" + std::to_string(round.code) + "\n";
    return out;
}

bool parse_state(const std::string &text, Round &out) {
    std::istringstream in(text);
    std::string line;
    bool saw_step = false;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "step") {
            out.step = std::strtoull(value.c_str(), nullptr, 10);
            saw_step = true;
        } else if (key == "finished") {
            out.finished = (value == "1");
        } else if (key == "code") {
            out.code = std::atoi(value.c_str());
        }
    }
    return saw_step;
}

// 一路 mkdir，中間不存在就建。已經存在不算失敗。
bool ensure_dir(const std::string &path) {
    if (exists(path)) return true;
    const std::size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        if (!ensure_dir(path.substr(0, slash))) return false;
    }
    return ::mkdir(path.c_str(), 0700) == 0 || exists(path);
}

}  // namespace

std::string Round::step_dir(unsigned long long n) const {
    return dir + "/steps/" + std::to_string(n);
}

Controller::Decision Controller::Decision::dispatch(Call call) {
    Decision d;
    d.kind = Dispatch;
    d.next = std::move(call);
    return d;
}

Controller::Decision Controller::Decision::finish(int code) {
    Decision d;
    d.kind = Finish;
    d.code = code;
    return d;
}

Controller::Decision Controller::Decision::wait() { return Decision{}; }

bool load_round(const std::string &round_dir, Round &out) {
    out = Round{};
    out.dir = round_dir;
    const std::string text = slurp(round_dir + "/state");
    if (text.empty()) return false;
    return parse_state(text, out);
}

bool save_round(const Round &round) {
    // ★ 走 write_durable：round 狀態跟 exit 檔一樣是「結論」，
    //   讀到寫一半的、或斷電後遺失，都會讓 Round 的進度說謊。
    return aossimple::write_durable(round.dir + "/state", render_state(round));
}

Call start_round(const std::string &round_dir) {
    Round round;
    round.dir = round_dir;
    ensure_dir(round_dir + "/steps");
    save_round(round);

    Call tick;
    tick.argv = {kAgentKey, round_dir};
    // tick 自己也是一個 Call，所以五個路徑都要有。它們用 round 目錄底下的檔，
    // 跟 child 的證據分開放。
    tick.stdin_path = "/dev/null";
    tick.stdout_path = round_dir + "/tick.out";
    tick.stderr_path = round_dir + "/tick.err";
    tick.exit_path = round_dir + "/tick.exit";
    tick.cwd = round_dir;
    return tick;
}

AgentExecutor::AgentExecutor(Controller &controller, Sink &downstream)
    : controller_(controller), downstream_(downstream) {}

// tick 自己也是一個終點 Call —— 沒有別人會給它結論，所以這裡要寫它的 exit 檔。
// 這是 docs/ADDING-A-LOOP.md 第 7 節那條「如果你的 loop 是這個 Call 的終點，
// 就要自己寫 exit 檔」。
//
// ⚠ 但**你要等的不是這個檔，是 `<round>/exit`**。
//   同一個 Round 的每次 tick 共用 `<round>/tick.exit`，所以它的意思是
//   「最後一次 tick 的結果」，不是「這個 Round 的結論」。
//   tick 是內部記帳，Round 才是工作。
Outcome AgentExecutor::finish(const Call &call, Outcome outcome) {
    const int number = aossimple::exit_number(outcome);
    if (number >= 0) {
        aossimple::write_durable(call.exit_path, std::to_string(number) + "\n");
    }
    return outcome;
}

Outcome AgentExecutor::run(const Call &call) {
    if (call.argv.size() < 2 || call.argv[0] != kAgentKey) {
        return finish(call,
                      Outcome::rejected("agent tick 的 argv 要是 {\"agent\", \"<round 目錄>\"}"));
    }
    const std::string dir = call.argv[1];

    Round round;
    if (!load_round(dir, round)) {
        return finish(call, Outcome::rejected("讀不到 round 狀態：" + dir + "/state"));
    }
    if (round.finished) {
        // 重複的 tick（例如同一個 child 觸發了兩次）。★ 不是錯誤，也不重做。
        return finish(call, Outcome::exited(round.code));
    }

    // 上一個 child 的證據。★ 只讀不解讀。
    ChildResult last;
    if (round.step > 0) {
        last.exists = true;
        last.dir = round.step_dir(round.step);
        last.stdout_path = last.dir + "/stdout";
        last.stderr_path = last.dir + "/stderr";
        last.exit_path = last.dir + "/exit";
        // ★★ exit 檔不在 = **還不知道**，不是失敗。這裡只把事實傳下去，
        //    要不要等、要不要放棄，是 controller 的政策。
        if (exists(last.exit_path)) {
            last.done = true;
            last.code = std::atoi(slurp(last.exit_path).c_str());
        }
    }

    const Controller::Decision decision = controller_.decide(round, last);

    switch (decision.kind) {
        case Controller::Decision::Wait:
            // 什麼都不做。等下一次被叫醒。
            return finish(call, Outcome::exited(0));

        case Controller::Decision::Finish: {
            round.finished = true;
            round.code = decision.code;
            if (!save_round(round)) {
                return finish(call, Outcome::unknown("round 收工了，但狀態寫不進去：" + dir));
            }
            if (!aossimple::write_durable(dir + "/exit",
                                          std::to_string(decision.code) + "\n")) {
                return finish(call, Outcome::unknown("round 收工了，但 exit 檔寫不進去：" + dir));
            }
            return finish(call, Outcome::exited(decision.code));
        }

        case Controller::Decision::Dispatch: {
            const unsigned long long next = round.step + 1;
            const std::string sdir = round.step_dir(next);
            if (!ensure_dir(sdir)) {
                return finish(call, Outcome::launch_error(0, "建不了 step 目錄：" + sdir));
            }

            // ★★ 每個 child 有**自己的**證據路徑。controller 不填這些，
            //    由這裡統一填 —— 那條「一個 Call 的 exit 檔只能有一個人寫」的
            //    鐵則，這樣才不可能被寫錯。
            Call child = decision.next;
            child.stdout_path = sdir + "/stdout";
            child.stderr_path = sdir + "/stderr";
            child.exit_path = sdir + "/exit";
            if (child.cwd.empty()) child.cwd = dir;
            if (child.stdin_path.empty()) {
                // controller 沒指定就給一個空的 stdin，而不是留空讓驗證失敗。
                const std::string in = sdir + "/stdin";
                if (!exists(in)) aossimple::write_durable(in, "");
                child.stdin_path = in;
            }
            // continuation_observer 靠這個認出要叫醒誰。
            child.user = std::string{kUserPrefix} + dir;

            // ★★★ 順序：**先存進度，再派工**。
            //
            // 反過來的話，push 成功但存檔前崩潰，重開後 round.step 還是舊的，
            // 於是我們會再派一次 —— 而那個 child 可能已經跑過了，
            // 外部作用就發生了兩次。這正是 AOS 那條「先提交 dispatch intent，
            // 再真正啟動」。
            //
            // 反方向的失敗（存了但沒派出去）是**安全**的：round 停在那一步、
            // child 的 exit 檔永遠不出現，看起來就是「還不知道」——而那本來就
            // 不該自動重做。停住比重複好。
            round.step = next;
            if (!save_round(round)) {
                return finish(call, Outcome::unknown("進度寫不進去，沒有派工：" + dir));
            }
            if (!downstream_.push(child)) {
                return finish(call, Outcome::launch_error(0, "下游已收工，交不出去"));
            }
            return finish(call, Outcome::exited(0));
        }
    }
    return finish(call, Outcome::unknown("controller 回了一個不認得的決定"));
}

aossimple::Loop::Observer continuation_observer(Sink &agent_queue) {
    Sink *queue = &agent_queue;
    return [queue](const Call &call, const Outcome &) {
        if (!call.user) return;
        const std::string &who = *call.user;
        const std::string prefix = kUserPrefix;
        if (who.size() <= prefix.size() || who.compare(0, prefix.size(), prefix) != 0) {
            return;  // 不是 agent 派的 child，不關我們的事
        }
        const std::string dir = who.substr(prefix.size());

        // ★ 這裡刻意不看 Outcome。child 成功或失敗都要叫醒 Round ——
        //   「失敗了怎麼辦」是 controller 的政策，不是這裡的。
        //   ⚠ 但 Unknown 也會叫醒，那時候 child 的 exit 檔可能不存在，
        //     controller 拿到的就是 done=false。那是對的：它該自己決定要等、
        //     要放棄、還是要人來看。
        Call tick;
        tick.argv = {kAgentKey, dir};
        tick.stdin_path = "/dev/null";
        tick.stdout_path = dir + "/tick.out";
        tick.stderr_path = dir + "/tick.err";
        tick.exit_path = dir + "/tick.exit";
        tick.cwd = dir;
        queue->push(tick);
    };
}

}  // namespace aosagent
