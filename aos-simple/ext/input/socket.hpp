#pragma once
// socket.hpp — Unix domain socket 的輸入來源。
//
// ── ★★ 語義：收下就回 ───────────────────────────────────────────────
//
// 連線送一行 JSON，**立刻**拿到一行回覆，說的是「收下了沒有」——
// 不是「做完了沒有」。
//
//     client ──▶ {"argv":["exec","/bin/ls"], …}
//     server ──▶ {"ok":true}                      ← 這時候命令才剛進 queue
//
// 然後 client 自己去讀 `stdout`／`stderr`／`exit` 那三個檔。
// **`exit` 檔不在 = 還在跑（或還不知道）；在 = 這一次的結論。**
//
// ★ 這個選擇讓 loop 的形狀**完全不用動**：連線執行緒只做 parse 與 dispatch，
//   跟 stdin 那條路一模一樣，沒有任何從 `Outcome` 回到連線的路徑。
//   「等到做完」那條路會需要那個回頭路，並且要處理「連線先斷了怎麼辦」——
//   那才是會往回改 loop 的那個選項。
//
// ── 回覆一定跟請求一對一、照順序 ────────────────────────────────────
//
// 沒有 request id，因為不需要：一行請求對一行回覆，順序就是對應關係。
//
//     {"ok":true}
//     {"ok":false,"error":"cwd 要是絕對路徑，拿到 tmp"}
//
// ⚠ `{"ok":true}` 只代表**排進去了**。命令本身的成敗在它自己的 exit 檔裡，
//   跟這個回覆無關。
//
// ── 併發 ────────────────────────────────────────────────────────────
//
// 一條連線一條執行緒，全程阻塞。跟 aos-core 同一個模型——因為「收下就回」
// 很快，這條執行緒不會待著不動，所以不需要事件迴圈。

#include <atomic>
#include <functional>
#include <string>

#include "../../src/call.hpp"

namespace aosinput {

// 決定 socket 路徑，規則跟 aos-core 一致：
//   1. 環境變數 AOS_SOCKET
//   2. $XDG_RUNTIME_DIR/aos-simple.sock
//   3. /tmp/aos-simple-<uid>.sock
std::string default_socket_path();

// 一行請求的處理結果，會被翻成回覆那一行。
struct Ack {
    bool ok = false;
    std::string error;  // ok 為 false 時的原因

    static Ack accepted() { return Ack{true, {}}; }
    static Ack refused(std::string why) { return Ack{false, std::move(why)}; }
};

class SocketServer {
  public:
    // handler 拿到一個已經 parse＋驗證過的 Call，回答收不收。
    // ★ 它會被**多條連線執行緒**同時呼叫，要自己顧共用狀態
    //  （`Router` 的 dispatch 走的是各 Channel 自己的鎖，所以是安全的）。
    using Handler = std::function<Ack(aossimple::Call)>;

    SocketServer(std::string path, Handler handler);
    ~SocketServer();

    SocketServer(const SocketServer &) = delete;
    SocketServer &operator=(const SocketServer &) = delete;

    // 綁定並開始監聽。失敗回 false 並把原因寫進 error()。
    bool listen();

    // 接受連線直到 stop() 被呼叫。會擋住。
    void serve();

    // ★ 可以從**信號處理常式**呼叫：它只寫一個 byte 進 self-pipe，
    //   那是 async-signal-safe 的。
    void stop();

    const std::string &error() const { return error_; }
    const std::string &path() const { return path_; }

    unsigned long long accepted() const { return accepted_.load(); }
    unsigned long long refused() const { return refused_.load(); }
    unsigned long long connections() const { return connections_.load(); }

  private:
    void handle_connection(int fd);

    std::string path_;
    Handler handler_;
    std::string error_;
    int listen_fd_ = -1;
    int wake_read_ = -1;
    int wake_write_ = -1;
    bool bound_ = false;

    std::atomic<unsigned long long> accepted_{0};
    std::atomic<unsigned long long> refused_{0};
    std::atomic<unsigned long long> connections_{0};
};

}  // namespace aosinput
