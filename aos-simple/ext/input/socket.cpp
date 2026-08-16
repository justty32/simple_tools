#include "socket.hpp"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>

#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <thread>
#include <vector>

#include "ndjson.hpp"

namespace aosinput {
namespace {

// JSON 字串的跳脫。★ 錯誤訊息裡有中文、引號和路徑，不跳脫的話回覆會是壞 JSON。
//   非 ASCII 原樣輸出（UTF-8 本來就是合法的 JSON 字串內容），只處理必須跳脫的。
std::string escape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char *hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += hex[static_cast<unsigned char>(c) & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string render(const Ack &ack) {
    if (ack.ok) return "{\"ok\":true}\n";
    return "{\"ok\":false,\"error\":\"" + escape(ack.error) + "\"}\n";
}

bool write_all(int fd, const std::string &data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::write(fd, data.data() + sent, data.size() - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;  // 對方多半斷線了
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

std::string default_socket_path() {
    if (const char *explicit_path = std::getenv("AOS_SOCKET")) {
        if (explicit_path[0] != '\0') return explicit_path;
    }
    if (const char *runtime = std::getenv("XDG_RUNTIME_DIR")) {
        if (runtime[0] != '\0') return std::string{runtime} + "/aos-simple.sock";
    }
    return "/tmp/aos-simple-" + std::to_string(::getuid()) + ".sock";
}

SocketServer::SocketServer(std::string path, Handler handler)
    : path_(std::move(path)), handler_(std::move(handler)) {}

SocketServer::~SocketServer() {
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (wake_read_ >= 0) ::close(wake_read_);
    if (wake_write_ >= 0) ::close(wake_write_);
    if (bound_) ::unlink(path_.c_str());
}

bool SocketServer::listen() {
    // ★ 對方中途離開時寫進斷掉的 socket 會發 SIGPIPE，預設行為是殺掉行程——
    //   不擋的話 client 按個 Ctrl-C 就能讓整台服務消失。
    //   （子行程那邊會被還原成 SIG_DFL，見 src/exec.cpp。）
    ::signal(SIGPIPE, SIG_IGN);

    if (path_.size() >= sizeof(sockaddr_un{}.sun_path)) {
        error_ = "socket 路徑太長：" + path_;
        return false;
    }

    int wake[2];
    if (::pipe(wake) != 0) {
        error_ = std::string{"開不了喚醒管線："} + std::strerror(errno);
        return false;
    }
    wake_read_ = wake[0];
    wake_write_ = wake[1];

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        error_ = std::string{"開不了 socket："} + std::strerror(errno);
        return false;
    }

    // 舊的殘留檔會讓 bind 失敗。⚠ 這代表同時只能有一個 server 用同一個路徑；
    //   要擋重複啟動得另外加鎖檔，還沒做。
    ::unlink(path_.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path_.c_str(), path_.size() + 1);

    // ★ 先把 umask 收緊再 bind，這樣 socket 一出生就是 0600。
    //   bind 之後再 chmod 有一個窗口，那期間別人連得進來——而這條 socket
    //   可以叫起任意命令，權限開大等於把 shell 送給同機的其他使用者。
    const mode_t old_mask = ::umask(0177);
    const int rc = ::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr);
    ::umask(old_mask);
    if (rc != 0) {
        error_ = "bind " + path_ + " 失敗：" + std::strerror(errno);
        return false;
    }
    bound_ = true;

    if (::listen(listen_fd_, 64) != 0) {
        error_ = std::string{"listen 失敗："} + std::strerror(errno);
        return false;
    }
    return true;
}

void SocketServer::stop() {
    // ★ 只寫一個 byte。這是唯一 async-signal-safe 的做法，所以信號處理常式
    //   可以直接叫它。在這裡 close(listen_fd_) 會是 race：accept 可能正在用它。
    if (wake_write_ >= 0) {
        const char byte = 1;
        const ssize_t ignored = ::write(wake_write_, &byte, 1);
        static_cast<void>(ignored);
    }
}

void SocketServer::serve() {
    std::vector<std::thread> workers;

    for (;;) {
        pollfd fds[2];
        fds[0].fd = listen_fd_;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = wake_read_;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        const int ready = ::poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if ((fds[1].revents & POLLIN) != 0) break;  // stop() 被叫了
        if ((fds[0].revents & POLLIN) == 0) continue;

        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            break;
        }
        connections_.fetch_add(1);
        // 一條連線一條執行緒，全程阻塞。「收下就回」很快，所以這條執行緒
        // 不會待著不動。
        workers.emplace_back([this, fd] { handle_connection(fd); });
    }

    // ★ 等所有連線做完才回去。它們手上可能還有沒 dispatch 的請求，
    //   丟下它們等於安靜地掉工作。
    for (std::thread &t : workers) {
        if (t.joinable()) t.join();
    }
}

void SocketServer::handle_connection(int fd) {
    std::string pending;  // 還沒湊成一整行的
    char chunk[8192];
    bool alive = true;

    while (alive) {
        const ssize_t n = ::read(fd, chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;  // 對方關了寫端
        pending.append(chunk, static_cast<std::size_t>(n));

        for (;;) {
            const std::size_t nl = pending.find('\n');
            if (nl == std::string::npos) break;
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            // 空行與註解跳過，跟 stdin 那條路一致。★ 但**不回覆**——
            //   回覆是一行請求對一行回覆，跳過的行不算一行請求。
            const std::size_t first = line.find_first_not_of(" \t");
            if (first == std::string::npos || line[first] == '#') continue;

            const ParsedLine parsed = parse_call_line(line);
            Ack ack;
            if (!parsed.ok) {
                ack = Ack::refused(parsed.error);
            } else {
                ack = handler_ ? handler_(parsed.call) : Ack::refused("沒有接 handler");
            }

            if (ack.ok) accepted_.fetch_add(1);
            else refused_.fetch_add(1);

            // ⚠ 寫失敗就結束這條連線。★ 但**請求已經排進去了**，不會收回——
            //   「收下就回」的收下是真的收下，client 斷線不影響它。
            //   client 重連之後照樣可以去讀那些 exit 檔。
            if (!write_all(fd, render(ack))) {
                alive = false;
                break;
            }
        }
    }

    // 尾巴有沒收尾的半行就丟掉。⚠ 不當成請求：半行 JSON 解析出來的東西
    //   不會是使用者的本意。
    ::close(fd);
}

}  // namespace aosinput
