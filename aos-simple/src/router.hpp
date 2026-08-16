#pragma once
// router.hpp — 看 argv[0] 決定這通呼叫該進哪一個 queue。
//
// ── 為什麼在 queue 之前，而不是在 Executor 裡分流 ────────────────────
//
// 因為分流的目的就是**換一條 loop**。llm-ask 那種東西是有自己節奏的（會等網路、
// 可能要排隊、可能要限流），跟一般 exec 擠在同一條 loop 上的話，一個慢的
// llm-ask 會把後面所有普通命令一起卡住——loop 是一次跑一個的。
//
// 分到不同的 queue 之後，每條 loop 有自己的節奏，互不影響。
//
//     router.route("llm-ask", llm_queue);
//     router.fallback(exec_queue);
//
//     argv[0] == "llm-ask"    -> llm_queue
//     argv[0] == "/bin/ls"    -> exec_queue
//
// ── 比對規則：argv[0] 整串精確比對 ──────────────────────────────────
//
// 不取 basename、不做前綴、不做萬用字元。理由是**可預測**：
// 註冊了什麼字串就只有那個字串會命中，看名字就知道會去哪。
//
// 想讓 /usr/bin/llm-ask 也走同一條，就把那個字串也註冊上去。
// 明寫兩條比「自動猜 basename 相同就算同一個」要好——後者會讓
// /opt/evil/llm-ask 也被路由走，那不是使用者想要的。
//
// ★ 這也是為什麼 argv[0] 不必是路徑：像 llm-ask 這種**邏輯名字**根本不會被
//   exec，它只是路由的鍵。所以「argv[0] 要長什麼樣」是各個 Executor 自己的事，
//   不是共通驗證的事。

#include <map>
#include <string>
#include <vector>

#include "channel.hpp"
#include "loop.hpp"

namespace aossimple {

// 把 argv[0] 那個**路由鍵**吃掉，剩下的才是真正要跑的命令，然後交給 inner。
//
//     aos:  exec /bin/ls -la
//     跑的: /bin/ls -la
//
// ★ 路由鍵不是要執行的東西，是「往哪走」的指示。目的地拿到手之後就該把它拿掉，
//   否則 ExecExecutor 會傻傻地去找一支叫 exec 的執行檔。
//
// 吃掉之後 argv 空了（例如只送了 `exec` 沒帶命令）就是形狀不對，
// inner 會把它拒絕掉——這正是我們要的錯誤。
class DropArgv0Executor : public Executor {
  public:
    explicit DropArgv0Executor(Executor &inner) : inner_(inner) {}
    Outcome run(const Call &call) override;

  private:
    Executor &inner_;
};

class Router {
  public:
    // 註冊一個名字。同一個名字再註冊一次會覆蓋前一個。
    void route(std::string argv0, Sink &sink);

    // 沒命中任何名字時去哪。不設就沒有——dispatch 會回 NoRoute。
    void fallback(Sink &sink);

    enum class Result {
        Routed,    // 進了某個註冊的 queue
        FellBack,  // 進了 fallback
        NoRoute,   // 沒命中，也沒有 fallback
        Closed,    // 找到了 queue，但它已經收工，沒收下
    };

    struct Decision {
        Result result = Result::NoRoute;
        std::string destination;  // 命中的名字；fallback 是 "*"；沒有就是空的
    };

    Decision dispatch(const Call &call);

    // 目前註冊了哪些名字，照字典序。
    std::vector<std::string> names() const;

  private:
    std::map<std::string, Sink *> routes_;
    Sink *fallback_ = nullptr;
};

const char *to_string(Router::Result result);

}  // namespace aossimple
