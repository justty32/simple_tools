#pragma once
// exec.hpp — 真的去跑一個程式的執行者。
//
// ── 動作順序（順序是有意義的）──────────────────────────────────────
//
//   1. validate()             純驗證，還沒有任何外部作用
//   2. unlink(exit)           ★ 清掉上一次的結論
//   3. open stdin/out/err     第一個真正的外部作用（O_TRUNC）
//   4. fork + exec            不經 shell
//   5. waitpid                拿原始 status
//   6. 原子寫 exit            先寫 <exit>.tmp 再 rename
//
// ── ★ 第 2 步為什麼重要 ─────────────────────────────────────────────
//
// 因為輸出是**截斷覆寫**的（`>` 的語義），所以「檔案存在」本身不再代表
// 「這一次跑完了」——上一次留下的檔也在那裡。先把 exit 檔 unlink 掉，
// 就把這個性質換回來了：
//
//     exit 檔不在  =  還沒有結論（還在跑，或我們自己死了）
//     exit 檔在    =  這一次的結論，而且是完整的（rename 是原子的）
//
// 而且 stdout 檔在不在還能再細分一次：exit 不在、stdout 也不在 = 連開檔都還沒到；
// exit 不在但 stdout 在 = **可能已經跑過了**，絕對不可以自動重跑。
//
// ── ★ 三條串流走檔案，所以不會跟子行程互卡 ──────────────────────────
//
// loop 是一次跑一個、全程阻塞的。如果三條串流是管線，一個輸出量大的子行程會塞滿
// pipe buffer 然後跟我們互等，整條 loop 就停在那裡。走檔案這一整類問題不存在，
// 所以下面可以老實 fork 完就 waitpid，不必開讀取執行緒。
//
// ── ★★ 為什麼一定要 waitpid 的原始 status ──────────────────────────
//
//     exit 137     raw=0x8900  WIFEXITED=1   WEXITSTATUS=137
//     kill -9      raw=0x0009  WIFSIGNALED=1 WTERMSIG=9
//
// 只看「128+signal」那個合成數字的話這兩件事一模一樣。Outcome 分得開；
// exit 檔裡那個數字分不開（見 exit_number 的說明）。
//
// ── ★ exec 失敗要用 self-pipe 偵測 ─────────────────────────────────
//
// 子行程在 exec 前開一條 CLOEXEC 管線：exec 成功的話寫端自動關閉、父行程讀到 EOF；
// exec 失敗就把 errno 寫進去。沒有這個機制的話「執行檔不存在」跟「程式自己回 127」
// 長得一模一樣。

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "loop.hpp"

namespace aossimple {

// 沒給 env 檔時用的那份乾淨環境（"KEY=VALUE" 一項一個）。
//
// ★★ 預設是**乾淨**的，不是繼承本行程的。繼承的話同一個 Call 在不同呼叫端手上
//   會跑出不同結果（誰的 shell 剛好 export 了什麼就影響誰），證據就變成
//   「在某台機器某個 shell 底下」才成立。乾淨的預設讓結果只取決於 Call 本身。
//
// 給了 env 檔的話，那個檔是**在這份乾淨環境之上**被 source 的，
// 所以 `export PATH="$PATH:/opt/bin"` 的結果是可預測的。
std::vector<std::string> default_environment();

// 原子＋耐久地寫一個小檔：`.tmp` -> `fsync` -> `rename` -> `fsync` 目錄。
//
// ★ 任何要把「結論」落地的地方都該用這個，不要自己寫一遍——那三段各自擋不同的
//   災難，順序也是有意義的（詳見 exec.cpp 裡的實作註解）。
//   agent 擴充的 round 狀態與 exit 檔走的就是這個。
bool write_durable(const std::string &path, const std::string &content);

// exit 檔裡那一個數字。沿用 shell 的既有慣例，能分開的盡量分開：
//
//     Exited N      -> N
//     Signalled S   -> 128 + S
//     LaunchError   -> 127（ENOENT／ENOTDIR，找不到）／126（找得到但起不來）
//     Rejected      -> 125（我們自己擋下來的，什麼都沒發生）
//     逾時被砍       -> 124（沿用 timeout(1) 的慣例）
//     輸出爆量被砍   -> 123（★ 這個沒有標準，是我們自己定的）
//     Unknown       -> 不寫檔（見下）
//
// ⚠ 一個數字表達不了「程式自己 exit(137)」和「被 SIGKILL 殺死」的差別——兩者都是
//   137。這是「只寫一個數字」這個選擇的固有代價，不是實作沒做到；完整的區分留在
//   Outcome 裡。
//
// Unknown 回 -1，呼叫端據此**不寫檔**：檔案不在才代表「還不知道」，
// 硬塞一個數字進去等於謊稱有結論。
int exit_number(const Outcome &outcome);

// 一次執行的限制。★ `Call` 裡**沒有**這些欄位，也不會有——它們是各條 loop 的
// 政策，不是呼叫的形狀。跟 `PriorityQueue` 收優先值函式是同一招。
//
// 例：llm-ask 自己解析 `--wait-timeout`，並壓上自己的天花板
//
//     ExecExecutor exec{[](const Call &c) {
//         ExecOptions o;
//         o.timeout = std::min(parse_wait_timeout(c.argv), kLlmAskCeiling);
//         return o;
//     }};
struct ExecOptions {
    // 0 = 不限。★ 不限的時候走的是阻塞 waitpid，完全沒有輪詢成本，
    //   所以「不用逾時」的人不必付任何代價。
    std::chrono::milliseconds timeout{0};

    // SIGTERM 到 SIGKILL 之間給多久。★ 先 TERM 再 KILL 是為了讓子行程有機會
    //   自己收尾（刷 buffer、刪暫存檔）；直接 KILL 誰都來不及。
    std::chrono::milliseconds grace{5000};

    // 0 = 不限。stdout ＋ stderr 的總位元組數上限。
    long long max_output_bytes = 0;
};

class ExecExecutor : public Executor {
  public:
    // 每次執行都會問一次 policy。★ 它會被多條 worker 同時呼叫，要自己顧共用狀態。
    //   不給就是「都不限」，跟以前的行為一樣。
    using Policy = std::function<ExecOptions(const Call &)>;

    explicit ExecExecutor(Policy policy = nullptr);

    Outcome run(const Call &call) override;

  private:
    Policy policy_;
};

}  // namespace aossimple
