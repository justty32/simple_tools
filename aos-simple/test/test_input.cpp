// 外部輸入：JSON 解析與 NDJSON -> Call。
// ★ 純解析，不碰檔案系統、不起子行程——所以它跟 test_core 同一邊。

#include <sstream>
#include <string>
#include <vector>

#include "../ext/input/json.hpp"
#include "../ext/input/ndjson.hpp"
#include "check.hpp"

using namespace aosinput;

namespace {

// 一行合法的請求，測試再各自改壞其中一項。
std::string good_line(const std::string &extra = "") {
    return std::string{"{\"argv\":[\"exec\",\"/bin/ls\"],"} +
           "\"stdin\":\"/r/1/in\",\"stdout\":\"/r/1/out\"," +
           "\"stderr\":\"/r/1/err\",\"exit\":\"/r/1/exit\",\"cwd\":\"/tmp\"" + extra + "}";
}

void test_json() {
    check::section("JSON：只認得字串與字串陣列");

    JsonObject o;
    std::string err;

    check::ok(parse_object("{}", o, err), "空 object");
    check::ok(parse_object(" \t {\"a\":\"b\"} \n ", o, err), "前後空白沒關係");
    check::eq(o["a"].string(), std::string{"b"}, "取得出值");

    check::ok(parse_object("{\"a\":[\"x\",\"y\"]}", o, err), "字串陣列");
    check::eq(o["a"].array().size(), std::size_t{2}, "兩個元素");
    check::ok(parse_object("{\"a\":[]}", o, err), "空陣列");

    // ★ 用不到的型別一律拒絕，不是忽略
    check::ok(!parse_object("{\"a\":5}", o, err), "★ 數字拒絕");
    check::ok(!parse_object("{\"a\":true}", o, err), "★ 布林拒絕");
    check::ok(!parse_object("{\"a\":null}", o, err), "★ null 拒絕");
    check::ok(!parse_object("{\"a\":{\"b\":\"c\"}}", o, err), "★ 巢狀 object 拒絕");
    check::ok(!parse_object("{\"a\":[[\"x\"]]}", o, err), "★ 巢狀陣列拒絕");
    check::ok(!parse_object("{\"a\":[1]}", o, err), "★ 陣列裡的數字也拒絕");

    // 語法錯
    check::ok(!parse_object("", o, err), "空字串拒絕");
    check::ok(!parse_object("{\"a\":\"b\"", o, err), "沒收尾拒絕");
    check::ok(!parse_object("{\"a\" \"b\"}", o, err), "少冒號拒絕");
    check::ok(!parse_object("{\"a\":\"b\",}", o, err), "尾隨逗號拒絕");
    check::ok(!parse_object("{\"a\":\"b\"} 垃圾", o, err), "★ 尾巴有東西拒絕，不是安靜忽略");
    check::ok(!parse_object("{\"a\":\"b\",\"a\":\"c\"}", o, err), "★ 重複 key 拒絕");
    check::ok(!err.empty(), "錯誤訊息不是空的");

    // 跳脫
    check::ok(parse_object("{\"a\":\"x\\ny\\t\\\"z\\\"\\\\\"}", o, err), "基本跳脫");
    check::eq(o["a"].string(), std::string{"x\ny\t\"z\"\\"}, "解得對");
    check::ok(!parse_object("{\"a\":\"x\\qy\"}", o, err), "不認得的跳脫拒絕");

    // ★ 沒跳脫的控制字元要擋——一行一個物件的格式會被裸換行整個弄壞
    check::ok(!parse_object("{\"a\":\"x\ny\"}", o, err), "★ 裸換行拒絕");

    // ★ \u 與 UTF-8。路徑跟 user 都可能有中文
    check::ok(parse_object("{\"a\":\"\\u4e2d\\u6587\"}", o, err), "\\u 解得動");
    check::eq(o["a"].string(), std::string{"中文"}, "★ \\u4e2d\\u6587 => 中文");
    check::ok(parse_object("{\"a\":\"中文\"}", o, err), "直接放 UTF-8 也可以");
    check::eq(o["a"].string(), std::string{"中文"}, "原樣留著");

    // ★ surrogate pair（BMP 以外，例如 emoji）
    check::ok(parse_object("{\"a\":\"\\ud83d\\ude00\"}", o, err), "surrogate pair");
    check::eq(o["a"].string(), std::string{"\xF0\x9F\x98\x80"}, "★ U+1F600 轉成四位元組 UTF-8");
    check::ok(!parse_object("{\"a\":\"\\ud83d\"}", o, err), "落單的 high surrogate 拒絕");
    check::ok(!parse_object("{\"a\":\"\\ude00\"}", o, err), "落單的 low surrogate 拒絕");
    check::ok(!parse_object("{\"a\":\"\\u12\"}", o, err), "\\u 位數不夠拒絕");
}

void test_line_to_call() {
    check::section("★ 一行 JSON -> Call");

    {
        const ParsedLine p = parse_call_line(good_line());
        check::ok(p.ok, "合法的一行");
        check::eq(p.call.argv.size(), std::size_t{2}, "argv 兩項");
        check::eq(p.call.argv[0], std::string{"exec"}, "路由鍵");
        check::eq(p.call.stdin_path, std::string{"/r/1/in"}, "stdin -> stdin_path");
        check::eq(p.call.exit_path, std::string{"/r/1/exit"}, "exit -> exit_path");
        check::eq(p.call.cwd, std::string{"/tmp"}, "cwd");
        check::ok(!p.call.env.has_value(), "沒給 env 就是沒有");
        check::ok(!p.call.user.has_value(), "沒給 user 就是沒有");
    }

    {
        const ParsedLine p = parse_call_line(good_line(",\"user\":\"代理人-7\",\"env\":\"/etc/p\""));
        check::ok(p.ok, "帶可選欄位");
        check::eq(p.call.user.value(), std::string{"代理人-7"}, "★ user 的中文原樣過去");
        check::eq(p.call.env.value(), std::string{"/etc/p"}, "env");
    }

    // ★★ 不認得的 key 要拒絕，不是忽略
    {
        const ParsedLine p = parse_call_line(good_line(",\"usr\":\"me\""));
        check::ok(!p.ok, "★★ 打錯字的 key 被拒絕，不是安靜少一個 user");
        check::ok(p.error.find("usr") != std::string::npos, "而且指得出是哪個");
    }
    {
        const ParsedLine p = parse_call_line(good_line(",\"_type\":\"exec\""));
        check::ok(!p.ok, "★ 沒有 _type 這個欄位——形狀是固定的");
    }

    // 少必填
    for (const char *missing : {"argv", "stdin", "stdout", "stderr", "exit", "cwd"}) {
        std::string line = good_line();
        // 把那個 key 改名，等於少了它（同時也會撞上「不認得的 key」，
        // 所以改成直接砍掉整段比較準）
        const std::string needle = std::string{"\""} + missing + "\":";
        const std::size_t at = line.find(needle);
        const std::size_t end = line.find(needle == "\"argv\":" ? ']' : '"', at + needle.size() + 1);
        line.erase(at, end - at + 2);  // 連後面的逗號一起（最後一個欄位不會被挑到）
        const ParsedLine p = parse_call_line(line);
        check::ok(!p.ok, std::string{"少了 "} + missing + " 要拒絕");
    }

    // 型別錯
    check::ok(!parse_call_line("{\"argv\":\"exec\",\"stdin\":\"/a\",\"stdout\":\"/a\","
                               "\"stderr\":\"/a\",\"exit\":\"/a\",\"cwd\":\"/a\"}").ok,
              "argv 不是陣列要拒絕");

    // ★ 形狀驗證在這裡就做掉，壞的請求不用進 queue
    {
        std::string line = good_line();
        const std::size_t at = line.find("/tmp");
        line.replace(at, 4, "relative");
        const ParsedLine p = parse_call_line(line);
        check::ok(!p.ok, "★ 相對路徑在入口就被擋下來，不用進 queue");
        check::ok(p.error.find("cwd") != std::string::npos, "說得出是哪個欄位");
    }

    // ★ 但 argv[0] 長什麼樣不歸這裡管——那是目的地的事
    {
        std::string line = good_line();
        const std::size_t at = line.find("\"exec\"");
        line.replace(at, 6, "\"llm-ask\"");
        check::ok(parse_call_line(line).ok,
                  "★ argv[0] 是邏輯名字照過——共通入口不預判目的地的規矩");
    }
}

void test_read_stream() {
    check::section("★ 一個壞行傷不到下一行");

    std::ostringstream text;
    text << good_line() << "\n";
    text << "\n";                                  // 空行
    text << "  # 這是註解\n";                       // 註解
    text << "{壞掉的 JSON\n";                       // 壞行
    text << good_line(",\"user\":\"second\"") << "\n";
    text << "{\"argv\":[\"x\"]}\n";                 // 少必填
    text << good_line(",\"user\":\"third\"") << "\r\n";  // CRLF

    std::istringstream in{text.str()};
    std::vector<aossimple::Call> got;
    std::vector<unsigned long long> bad_lines;

    const ReadStats stats = read_ndjson(
        in, [&](aossimple::Call c) { got.push_back(std::move(c)); },
        [&](unsigned long long n, const std::string &) { bad_lines.push_back(n); });

    check::eq(stats.accepted, 3ull, "★ 三個好的都收到了");
    check::eq(stats.rejected, 2ull, "兩個壞的");
    check::eq(stats.blank, 2ull, "空行與註解跳過，不算錯");
    check::eq(got.size(), std::size_t{3}, "交出去三個 Call");
    check::eq(bad_lines.size(), std::size_t{2}, "兩個錯誤回報");
    check::eq(bad_lines[0], 4ull, "★ 回報得出是第幾行");
    check::eq(bad_lines[1], 6ull, "第二個壞行");
    check::eq(got[2].user.value(), std::string{"third"},
              "★ CRLF 的行也讀得動（\\r 有被去掉）");
    check::eq(got[1].user.value(), std::string{"second"},
              "★★ 壞行後面那個照樣進來——一個壞請求傷不到下一個");
}

}  // namespace

int main() {
    test_json();
    test_line_to_call();
    test_read_stream();
    return check::report();
}
