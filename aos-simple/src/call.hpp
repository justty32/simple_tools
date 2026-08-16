#pragma once
// call.hpp — 一次呼叫。**形狀是固定的**，沒有版本欄位、沒有型別欄位。
//
// ── 固定是刻意的 ────────────────────────────────────────────────────
//
// 沒有 _ver 也沒有 _type，所以：
//
//   * 沒有「這個版本支不支援」這條分支；
//   * 沒有「依 _type 去啃剩下的 key」這條分支；
//   * 一個 Call 拿在手上就是完整的，不必再問它是什麼。
//
// 之後真的需要第二種呼叫（python 之類）的時候，那是**再開一個型別**的事，
// 不是在這個結構裡加一個欄位然後到處 if。這一版的簡單就建立在這件事上。
//
// ── 欄位 ────────────────────────────────────────────────────────────
//
//   argv    要跑什麼。一串字串
//   stdin   ┐
//   stdout  ├ 三條串流。★ 不是位元組，是**位址**——由具體執行者去讀寫
//   stderr  ┘
//   exit    結論要寫到哪
//   cwd     在哪裡跑
//   env     exec 前要 source 的檔。★ 可選
//   user    誰發起的。★ 可選，也是唯一不是路徑的（除了 argv）
//
// 除了 argv 和 user，其他全部是路徑。

#include <optional>
#include <string>
#include <vector>

namespace aossimple {

struct Call {
    std::vector<std::string> argv;

    // ⚠ 不能叫 stdin／stdout／stderr：那三個在 <cstdio> 裡是**巨集**，
    //   成員名撞上去會被展開成一團看不懂的東西。所以一律加 _path。
    std::string stdin_path;
    std::string stdout_path;
    std::string stderr_path;

    std::string exit_path;
    std::string cwd;

    // exec 之前要 source 的檔案。沒給就直接繼承現在的環境（跟以前一樣）。
    // ★ 給了的話語義是「先 source，再拿結果去跑」——所以是**繼承後再修改**，
    //   不是整套換掉。細節見 exec.hpp。
    std::optional<std::string> env;

    // 沒有就是沒有。★ 用 optional 而不是空字串：空字串是一個合法的標示符
    //   （雖然沒人會這樣用），把「沒給」和「給了空的」混成同一件事，
    //   之後一定會有人踩到。env 同理——空字串不是合法路徑，但用同一套表達方式。
    std::optional<std::string> user;
};

// 驗證的結果。ok() 為真就是可以拿去跑。
struct Check {
    bool ok = true;
    std::string reason;  // ok 的時候是空的

    static Check pass() { return Check{}; }
    static Check fail(std::string why) { return Check{false, std::move(why)}; }
    explicit operator bool() const { return ok; }
};

// 純函式：只看形狀，**不碰檔案系統**。
//
// 檢查的東西：
//   * argv 不能是空的，每一項不能含 NUL，argv[0] 不能是空字串
//   * 五個路徑都要絕對路徑、不能含 NUL、不能是空的
//   * env 給了的話比照路徑辦理
//   * user 給了的話不能含 NUL（其餘不管——它是不透明標示符，不是路徑）
//
// 「這個路徑存不存在」不在這裡，那要碰檔案系統，屬於執行者。
Check validate(const Call &call);

}  // namespace aossimple
