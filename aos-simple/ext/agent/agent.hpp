#pragma once
// agent.hpp — AOS 的 Agent Round loop，做成**擴充**。
//
// ★★ 核心（src/）一行都不用改，也一行都不認識 agent。這裡只用三個公開的縫：
//     Executor（做事）、Sink（把工作交出去）、Loop::Observer（被通知做完了）。
//
// 名詞照 agent-machine/full/10-STEP-TOOL-ROUND.md：
//
//     Step   有原子操作角色的 Function
//     Tool   Agent 可用的 Function，最底層就是一個 Linux process
//     Round  由一串 Step 與 Tool 呼叫組成的 Function
//     Task   正在執行中的 Function 實例（**不是 PID**）
//
// ── ★★ 為什麼 Round 是狀態機，不是一個 while 迴圈 ────────────────────
//
// 直覺會想這樣寫：
//
//     while (!done) { auto call = decide(); dispatch(call); wait(call); }
//                                                          ^^^^^^^^^^^ ✗
//
// 不行。`wait` 會把這條 worker 佔住，而下游（exec loop）滿載時它的工作可能
// 間接要等 agent loop 有空 —— 死鎖，而且要湊巧才會發生、幾乎重現不了。
// 這正是 docs/ADDING-A-LOOP.md 6-4 那一條。
//
// 而且 Round 會跨行程重開：一個跑三天的 Round 不可能靠一個 C++ 堆疊活著。
//
// 所以 Round 的進度**存在磁碟上**，一次 tick 只做一小步：
//
//     一次 tick = 讀上一個 child 的證據 -> 問 controller -> 派下一個 child（或收工）
//
// 派完就返回，worker 立刻放掉。child 做完之後由下游的 observer 把這個 Round
// 再叫醒一次（`continuation_observer`）。沒有人在等任何人。
//
// 這就是 10-STEP-TOOL-ROUND.md 那句「Round controller 讀取已完成 child 的可信
// Return，**保存自己的進度**，再決定下一個 Function Call」。
//
// ── ★★ 這一層不認識 LLM ─────────────────────────────────────────────
//
// 「AOS Machine 不解析模型回覆」。prompt、messages、tool schema、模型回覆怎麼
// 拆成 tool call —— 全部在 `Controller` 這個介面**後面**，agent loop 自己也不碰。
//
// 模型呼叫本身照文件的建議「包成 curl-like executable adapter」：它就是一個
// 普通的 `exec` Call，跟別的 Tool 沒有兩樣。所以這個擴充**零網路、零 JSON、
// 零第三方相依**。
//
// ── 磁碟上長這樣 ────────────────────────────────────────────────────
//
//     <round>/
//       state              step=<n> / finished=<0|1> / code=<n>
//       exit               Round 自己的結論（一個數字，跟 Call 的 exit 同慣例）
//       steps/1/           第 1 個 child 的證據
//         stdin stdout stderr exit
//       steps/2/
//       …

#include <string>

#include "../../src/loop.hpp"
#include "../../src/channel.hpp"

namespace aosagent {

using aossimple::Call;
using aossimple::Executor;
using aossimple::Outcome;
using aossimple::Sink;

// 路由鍵。`{"agent", "<round 目錄>"}` 就是一次 tick。
inline constexpr const char *kAgentKey = "agent";

// child Call 的 `user` 欄位會被寫成 `agent:<round 目錄>`。
// ★ 這不是硬掰用途：`user` 的定義就是「誰發起的」，而發起這個 child 的
//   正是那個 Round。continuation_observer 靠它認出要叫醒誰。
inline constexpr const char *kUserPrefix = "agent:";

// 一個 Round 在磁碟上的進度。
struct Round {
    std::string dir;              // round 根目錄（絕對路徑）
    unsigned long long step = 0;  // 已經派出去幾個 child
    bool finished = false;
    int code = 0;

    // steps/<n>/ 的路徑。n 從 1 起算。
    std::string step_dir(unsigned long long n) const;
};

// 上一個 child 留下的證據。★ 只有證據，沒有解讀。
struct ChildResult {
    bool exists = false;   // 有沒有上一個 child（step == 0 時沒有）
    bool done = false;     // ★ exit 檔在不在。**不在 = 還不知道**，不是失敗
    int code = 0;          // done 時才有意義
    std::string dir;       // 那個 child 的證據目錄
    std::string stdout_path, stderr_path, exit_path;
};

// ★★ 整個擴充的縫。Agent 的全部智慧在這後面。
//
// ⚠ 實作**不可以阻塞**、不可以等 child、不可以打網路。它只做一件事：
//   看著證據，決定下一步。要打網路就派一個 child 去打（那才是 Tool）。
class Controller {
  public:
    virtual ~Controller() = default;

    struct Decision {
        enum Kind {
            Dispatch,  // 派下一個 child（Step 或 Tool）
            Finish,    // 這個 Round 結束了
            Wait,      // 還不能決定（上一個 child 還沒完成）。什麼都不做
        };
        Kind kind = Wait;

        // Dispatch 用。★ 只要填 argv（和可選的 env／stdin 來源）；
        //   stdout／stderr／exit／cwd 由 agent loop 填成 steps/<n>/ 底下的路徑，
        //   **呼叫端不要自己填**，那是「每個 child 有自己的證據路徑」這條鐵則
        //   唯一不會被寫錯的做法。
        Call next;

        int code = 0;  // Finish 用，會寫進 <round>/exit

        static Decision dispatch(Call call);
        static Decision finish(int code);
        static Decision wait();
    };

    // round 是目前進度（唯讀），last 是上一個 child 的證據。
    virtual Decision decide(const Round &round, const ChildResult &last) = 0;
};

// 一次 tick。掛在 agent 那條 loop 上。
class AgentExecutor : public Executor {
  public:
    // downstream 是 child Call 要送去的地方（通常是 exec 那條 loop 的 queue，
    // 或一個 Router 的轉接）。
    AgentExecutor(Controller &controller, Sink &downstream);

    // call.argv 必須是 {"agent", "<round 目錄>"}。
    Outcome run(const Call &call) override;

  private:
    // 寫這通 tick 自己的 exit 檔，然後把結論原樣傳回去。
    Outcome finish(const Call &call, Outcome outcome);

    Controller &controller_;
    Sink &downstream_;
};

// 掛在**下游** loop 的 observer 上：child 做完就把它的 Round 叫醒。
//
// ★ 下游完全不認識 agent —— 它只是照約定叫自己的 observer。認出
//   `user == "agent:<dir>"` 並把 tick 推回 agent queue 的是這個函式。
//
//     Pool exec_pool{exec_queue, exec_executor, 4,
//                    aosagent::continuation_observer(agent_queue)};
aossimple::Loop::Observer continuation_observer(Sink &agent_queue);

// 建一個新的 Round 目錄並寫好初始狀態。回傳可以丟進 agent queue 的第一個 tick。
// 目錄必須已經存在。
Call start_round(const std::string &round_dir);

// 讀／寫 round 狀態。給測試與外部觀察用。
bool load_round(const std::string &round_dir, Round &out);
bool save_round(const Round &round);

}  // namespace aosagent
