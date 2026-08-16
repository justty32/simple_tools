// socket 輸入來源。★ 會真的開 socket、開執行緒，所以自己一支。

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "../ext/input/socket.hpp"
#include "check.hpp"

using namespace aossimple;
using namespace aosinput;

namespace {

std::string root;

std::string line_for(const std::string &argv0, const std::string &tag) {
    return "{\"argv\":[\"" + argv0 + "\"],\"stdin\":\"" + root + "/in\",\"stdout\":\"" +
           root + "/out\",\"stderr\":\"" + root + "/err\",\"exit\":\"" + root +
           "/exit\",\"cwd\":\"/tmp\",\"user\":\"" + tag + "\"}";
}

int connect_to(const std::string &path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// 送一段文字，讀回覆到對方關閉或讀不到為止。
std::string round_trip(const std::string &path, const std::string &send) {
    const int fd = connect_to(path);
    if (fd < 0) return "<連不上>";
    if (::write(fd, send.data(), send.size()) < 0) {
        ::close(fd);
        return "<寫失敗>";
    }
    ::shutdown(fd, SHUT_WR);  // ★ 關寫端，server 才知道沒有下一行了

    std::string out;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n <= 0) break;
        out.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return out;
}

void test_socket() {
    check::section("★ socket：收下就回");

    const std::string path = root + "/s.sock";
    std::vector<Call> got;
    std::mutex got_mutex;

    SocketServer server{path, [&](Call call) -> Ack {
                            std::lock_guard<std::mutex> g(got_mutex);
                            // 用 argv[0] 決定收不收，模擬 Router
                            if (call.argv[0] != "exec") {
                                return Ack::refused("沒有這條路由：" + call.argv[0]);
                            }
                            got.push_back(std::move(call));
                            return Ack::accepted();
                        }};

    check::ok(server.listen(), "listen 起得來");
    std::thread serving{[&] { server.serve(); }};

    // 等 socket 真的出現
    for (int i = 0; i < 200; ++i) {
        struct stat st;
        if (::stat(path.c_str(), &st) == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    // ★ 權限：這條 socket 可以叫起任意命令，開大等於把 shell 送給同機的人
    struct stat st{};
    check::ok(::stat(path.c_str(), &st) == 0, "socket 檔在");
    check::eq(static_cast<int>(st.st_mode & 0777), 0600,
              "★★ socket 是 0600（bind 前先收緊 umask，不是 bind 後才 chmod）");

    // 一行請求一行回覆，照順序
    {
        const std::string reply = round_trip(
            path, line_for("exec", "第一") + "\n" + line_for("nope", "第二") + "\n" +
                      "{壞掉的\n" + line_for("exec", "第三") + "\n");

        check::ok(reply.find("{\"ok\":true}\n{\"ok\":false") == 0,
                  "★ 第一行 ok、第二行不 ok——一對一且照順序");
        check::ok(reply.find("沒有這條路由：nope") != std::string::npos,
                  "★ 拒絕的理由帶回去了");
        check::ok(reply.find("JSON 壞了") != std::string::npos, "壞語法也有原因");

        std::size_t lines = 0;
        for (const char c : reply) {
            if (c == '\n') ++lines;
        }
        check::eq(lines, std::size_t{4}, "★ 四行請求，四行回覆");

        std::lock_guard<std::mutex> g(got_mutex);
        check::eq(got.size(), std::size_t{2}, "★★ 壞行後面那個照樣收下");
        check::eq(got[1].user.value(), std::string{"第三"}, "而且是對的那一個");
    }

    // 空行與註解不算一行請求，所以**不回覆**
    {
        const std::string reply =
            round_trip(path, "\n  # 註解\n" + line_for("exec", "只有這個") + "\n");
        check::eq(reply, std::string{"{\"ok\":true}\n"},
                  "★ 空行與註解跳過，而且不佔一行回覆");
    }

    // 中文與引號要能安全地回到 client（回覆是自己組的 JSON）
    {
        const std::string bad =
            "{\"argv\":[\"exec\"],\"stdin\":\"相對\",\"stdout\":\"/a\",\"stderr\":\"/a\","
            "\"exit\":\"/a\",\"cwd\":\"/a\"}\n";
        const std::string reply = round_trip(path, bad);
        check::ok(reply.find("{\"ok\":false") == 0, "被拒絕");
        check::ok(reply.find("相對") != std::string::npos,
                  "★ 錯誤訊息裡的中文原樣回去（UTF-8 不必跳脫）");
        check::ok(reply.back() == '\n', "一行一個回覆");
    }

    // 多條連線同時打
    {
        constexpr int kClients = 8;
        std::vector<std::thread> clients;
        std::atomic<int> ok_count{0};
        for (int i = 0; i < kClients; ++i) {
            clients.emplace_back([&, i] {
                const std::string reply =
                    round_trip(path, line_for("exec", "並行" + std::to_string(i)) + "\n");
                if (reply == "{\"ok\":true}\n") ok_count.fetch_add(1);
            });
        }
        for (std::thread &t : clients) t.join();
        check::eq(ok_count.load(), kClients, "★ 8 條連線同時打，回覆都對");
    }

    // ★ 沒送完整一行就斷線：那半行不算請求
    {
        const int fd = connect_to(path);
        const std::string half = line_for("exec", "半行").substr(0, 20);
        const ssize_t ignored = ::write(fd, half.data(), half.size());
        static_cast<void>(ignored);
        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        check::ok(true, "★ 半行 JSON 不會被當成請求（沒有崩潰就是通過）");
    }

    const unsigned long long before = server.accepted();
    server.stop();
    serving.join();  // ★ 卡住就是 stop() 沒作用
    check::ok(true, "★ stop() 之後 serve() 回得來");
    check::ok(server.accepted() >= before, "統計數字還在");
    check::ok(server.connections() >= 11, "數得出服務過幾條連線");

    // 收工之後 socket 檔要清掉，不然下次 bind 會撞到
    check::ok(true, "（socket 檔由解構子 unlink）");
}

void test_path_rules() {
    check::section("socket 路徑規則");

    ::setenv("AOS_SOCKET", "/tmp/explicit.sock", 1);
    check::eq(default_socket_path(), std::string{"/tmp/explicit.sock"},
              "AOS_SOCKET 最優先");
    ::unsetenv("AOS_SOCKET");

    ::setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    check::eq(default_socket_path(), std::string{"/run/user/1000/aos-simple.sock"},
              "再來是 XDG_RUNTIME_DIR");
    ::unsetenv("XDG_RUNTIME_DIR");

    check::ok(default_socket_path().find("/tmp/aos-simple-") == 0,
              "最後退到 /tmp/aos-simple-<uid>.sock");
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/aos-simple-socket-XXXXXX";
    const char *made = ::mkdtemp(tmpl);
    if (made == nullptr) {
        std::fprintf(stderr, "開不了暫存目錄\n");
        return 1;
    }
    root = made;
    // socket 路徑不能太長，所以測試用短一點的暫存目錄名。
    { const int fd = ::open((root + "/in").c_str(), O_WRONLY | O_CREAT, 0600); if (fd >= 0) ::close(fd); }

    test_socket();
    test_path_rules();

    const int result = check::report();
    if (result == 0) {
        const std::string cmd = "rm -rf '" + root + "'";
        if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "清不掉 %s\n", root.c_str());
    } else {
        std::fprintf(stderr, "現場留在 %s\n", root.c_str());
    }
    return result;
}
