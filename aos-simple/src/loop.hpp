#pragma once
// loop.hpp — 核心 loop，以及 exec 之後要接進來的那個縫。
//
// ── 整台機器就是這幾行 ──────────────────────────────────────────────
//
//     for (;;) {
//         auto call = queue.take();   // 取一個，空的就擋著等
//         if (!call) break;           // 取到哨兵 = 收工
//         handle(*call);              // 交給執行者
//     }
//
// loop 自己**只做兩件事：取，交出去**。驗證、執行、寫 exit 檔全部是 Executor 的事。
//
// ★ 驗證為什麼在 Executor 而不在這裡：因為「被拒絕」也是一個要留下紀錄的結論
//   （exit 檔要寫 125），而寫檔是執行者的職責。放在 loop 的話 loop 就得碰檔案
//   系統，那條「核心一個檔都不開」的界線就沒了。
//
// 這樣切的理由：exec 是整套裡唯一有外部作用、唯一難測、唯一跟平台綁死的一段。
// 把它關在一個介面後面，上面這幾行就可以在完全沒有子行程的情況下測完。

#include <cstddef>
#include <functional>
#include <string>

#include "call.hpp"
#include "channel.hpp"

namespace aossimple {

// 一次呼叫的結論。★ 五個值各自只代表一件事，不重疊。
enum class Status {
    Rejected,     // 驗證沒過，**根本沒送去執行**
    Exited,       // 跑完了，code 有意義
    Signalled,    // 被信號殺死，signal 有意義
    LaunchError,  // 起不來（執行檔不存在、沒權限…）
    Unknown,      // ★ 執行者沒能給出結論。不是失敗，是「不知道」——
                  //   不可以當成失敗處理，更不可以據此自動重跑
};

const char *to_string(Status status);

// ★ 這個結構保留完整資訊，跟 exit 檔裡那個數字是兩回事。
//   數字是給 shell 看的、會合流（exit 137 和 SIGKILL 都是 137）；
//   這裡不會，因為在記憶體裡多帶兩個 int 不花什麼成本。
struct Outcome {
    Status status = Status::Unknown;
    int code = 0;       // Exited 時有意義
    int signal = 0;     // Signalled 時有意義
    int err = 0;        // LaunchError 的 errno
    std::string reason; // Rejected／LaunchError／Unknown 的說明

    // ★★ 被我們自己砍掉的兩種原因。
    //
    // 沒有這兩個旗標的話，逾時／爆量看起來就是一次普通的 Signalled{SIGKILL}——
    // 跟「外面有人砍它」完全分不開。而這兩件事的後續處理差很多：
    // 逾時通常該調參數或放棄，被外人砍通常該重跑。
    bool timed_out = false;
    bool output_capped = false;

    static Outcome rejected(std::string why);
    static Outcome exited(int code);
    static Outcome signalled(int signal);
    static Outcome launch_error(int errnum, std::string why);
    static Outcome unknown(std::string why);
};

// exec 要接的縫。
//
// 實作要負責：驗證、執行、把結論寫進 call.exit_path。
//
// ⚠ 實作**不應該丟例外**——loop 會接住並轉成 Unknown，但那會讓一次真的執行
//   看起來像「不知道」，而那兩件事的後果完全不同（不知道不能重跑）。
//   有結論就回結論，包含失敗的結論。
class Executor {
  public:
    virtual ~Executor() = default;
    virtual Outcome run(const Call &call) = 0;
};

// 什麼都不做，誠實地說不知道。給測試和「先接線但還不想跑東西」用。
class DefaultExecutor : public Executor {
  public:
    Outcome run(const Call &call) override;
};

struct Stats {
    unsigned long long handled = 0;   // 取出來處理過幾個（含被拒絕的）
    unsigned long long rejected = 0;  // 其中驗證沒過的
};

class Loop {
  public:
    // 每處理完一個叫一次，成功失敗都叫。可以不給。
    using Observer = std::function<void(const Call &, const Outcome &)>;

    Loop(Source &queue, Executor &executor, Observer observer = nullptr);

    // ★ 核心 loop。擋著取，一次做一個，取到哨兵才回來。
    //   要它回來的唯一方法是 queue.close()。
    void run();

    // 取一個做一個，**不擋**：queue 空的話回 false。給測試和單次批次用。
    bool step();

    // 把此刻排著的全部做完，回傳做了幾個。不擋。
    std::size_t drain();

    Stats stats() const { return stats_; }

  private:
    void handle(const Call &call);

    Source &queue_;
    Executor &executor_;
    Observer observer_;
    Stats stats_;
};

}  // namespace aossimple
