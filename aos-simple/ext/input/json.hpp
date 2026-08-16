#pragma once
// json.hpp — 只認得 `Call` 需要的那一小塊 JSON。
//
// ── ★★ 刻意只支援一個平面 object ────────────────────────────────────
//
//     {"argv": ["exec", "/bin/ls"], "cwd": "/tmp", "user": "agent-7"}
//
// 值只能是**字串**或**字串陣列**。數字、布林、null、巢狀 object、巢狀陣列
// 一律**拒絕**，不是忽略。
//
// 這不是偷懶，是設計：`Call` 的形狀是固定的，而且每一個欄位都是字串或字串陣列
// （見 README〈形狀是固定的〉）。支援用不到的型別只會多出一堆「這個數字要怎麼
// 轉成路徑」之類的問題，那些問題沒有好答案。
//
// 嚴格也讓錯誤訊息有用：`{"cwd": 5}` 會告訴你 cwd 不是字串，
// 而不是安靜地變成 "5" 或空字串。
//
// ── 支援的跳脫 ──────────────────────────────────────────────────────
//
// `\" \\ \/ \b \f \n \r \t` 與 `\uXXXX`（含 surrogate pair，所以中文與
// emoji 都正確轉成 UTF-8）。★ 這一項不能省：路徑和 user 都可能有非 ASCII。

#include <map>
#include <string>
#include <vector>

namespace aosinput {

// 一個值：不是字串就是字串陣列。
class JsonValue {
  public:
    static JsonValue of_string(std::string s);
    static JsonValue of_array(std::vector<std::string> v);

    bool is_string() const { return is_string_; }
    bool is_array() const { return !is_string_; }
    const std::string &string() const { return string_; }
    const std::vector<std::string> &array() const { return array_; }

  private:
    bool is_string_ = true;
    std::string string_;
    std::vector<std::string> array_;
};

using JsonObject = std::map<std::string, JsonValue>;

// 解析一整段文字成一個 object。
//
// ⚠ 尾巴有多餘內容也算錯——`{"a":"b"} 垃圾` 會被拒絕，不是安靜忽略。
//   一行一個物件的格式下，尾巴有東西通常代表送的人搞錯了。
//
// 成功回 true；失敗回 false 並把原因寫進 error（會標出大概的位置）。
bool parse_object(const std::string &text, JsonObject &out, std::string &error);

}  // namespace aosinput
