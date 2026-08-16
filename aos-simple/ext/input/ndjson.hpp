#pragma once
// ndjson.hpp — 外部輸入：一行一個 JSON 物件，變成 `Call`。
//
// ★★ 這一層的工作只有一件：**把外面的請求變成 Call**，然後交給 Router。
//    它不碰 loop、不碰 exec、不知道有幾條 worker。
//
// ── 一行一個物件，不是串流式 JSON ───────────────────────────────────
//
//     {"argv":["exec","/bin/ls","-la"],"stdin":"/r/1/in","stdout":"/r/1/out",
//      "stderr":"/r/1/err","exit":"/r/1/exit","cwd":"/tmp","user":"agent-7"}
//     {"argv":["agent","/r/round-9"], …}
//
// ★ 一行一個的價值不是好寫，是**一個壞掉的請求傷不到下一個**。串流式 parser
//   碰到壞語法之後就不知道下一個物件從哪開始了，只能整條中斷；一行一個的話
//   壞的那行報掉、下一行照常。這跟「一個爛任務炸不掉 loop」是同一條規矩。
//
// ── ★ 呼叫端怎麼知道做完了 ──────────────────────────────────────────
//
// **輪詢 `exit` 檔。** 不在 = 還不知道，在 = 這一次的結論（一個數字）。
//
// 這是走 stdin 的好處：沒有連線要回應，所以不必決定「連線是等到做完還是收下就回」
// 那個會往回影響 loop 形狀的問題。之後要接 socket 的時候再面對它。
//
// ── 欄位 ────────────────────────────────────────────────────────────
//
// | JSON key | 對到          | 必填 |
// |----------|---------------|------|
// | argv     | argv          | ✓ 字串陣列 |
// | stdin    | stdin_path    | ✓ |
// | stdout   | stdout_path   | ✓ |
// | stderr   | stderr_path   | ✓ |
// | exit     | exit_path     | ✓ |
// | cwd      | cwd           | ✓ |
// | env      | env           |   可選 |
// | user     | user          |   可選 |
//
// ★ 沒有 `_ver`、沒有 `_type`。形狀是固定的，見 README。
//
// ★★ **不認得的 key 一律拒絕**，不是忽略。`{"usr":"me"}` 會報錯而不是安靜地
//    少一個 user——打錯字被安靜吞掉，是那種要很久以後才會發現的錯。

#include <functional>
#include <istream>
#include <string>

#include "../../src/call.hpp"

namespace aosinput {

// 一行的解析結果。
struct ParsedLine {
    bool ok = false;
    aossimple::Call call;
    std::string error;  // ok 為 false 時說明原因
};

// 解析一行。★ 只做「JSON -> Call」的轉換與**形狀**檢查，
//   不碰檔案系統（那是 Executor 的事），也不驗證 argv[0] 長什麼樣（那是目的地的事）。
ParsedLine parse_call_line(const std::string &line);

struct ReadStats {
    unsigned long long accepted = 0;
    unsigned long long rejected = 0;
    unsigned long long blank = 0;  // 空行與註解行，直接跳過
};

// 從 in 讀到 EOF。每個成功的 Call 交給 on_call，每個壞掉的行交給 on_error。
//
// ★ 壞行**不會**中斷讀取。on_error 拿到 (行號, 原因)，由呼叫端決定要記到哪。
// ★ 空行跳過；`#` 開頭的行也跳過（方便手寫測試檔）。
ReadStats read_ndjson(std::istream &in,
                      const std::function<void(aossimple::Call)> &on_call,
                      const std::function<void(unsigned long long, const std::string &)> &on_error);

}  // namespace aosinput
