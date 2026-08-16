#include "exec.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace aossimple {
namespace {

// 開檔／關檔的小包裝。exec 這段有很多條提早返回的路徑，用 RAII 才不會漏。
class Fd {
  public:
    Fd() = default;
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() { reset(); }
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;

    void reset(int fd = -1) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }
    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }

  private:
    int fd_ = -1;
};

std::string dirname_of(const std::string &path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == 0) return "/";
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

// 先寫 <path>.tmp、fsync、rename，再 fsync 目錄。
//
// ── ★★ 為什麼是兩次 fsync ───────────────────────────────────────────
//
// 這兩件事**擋的是不同的災難**，少一個都不完整：
//
//   1. rename 本身        擋「讀到半個檔」。它是原子的，所以讀的人看到的
//                         要嘛是「還沒有」，要嘛是「完整的一份」。
//                         這一項擋的是**行程崩潰**。
//
//   2. fsync(檔案)        把內容真的刷到碟片。少了它，斷電後那個檔可能存在
//                         但**是空的**——最糟的一種，因為它看起來像有結論。
//
//   3. fsync(目錄)        把「這個名字現在指向那個 inode」這件事刷到碟片。
//                         少了它，斷電後 rename 可能整個沒發生，檔案不存在。
//                         ⚠ 這一步最常被忘記：fsync 檔案不會順便保證目錄項目。
//
// 順序也是有意義的：一定要**先** fsync 檔案**再** rename。反過來的話，
// rename 已經生效但內容還沒落地，斷電就會留下一個空檔。
//
// 代價是每次呼叫多兩次磁碟同步。對「產生證據」這件事來說值得——exit 檔的
// 全部意義就是「它在 = 結論已知」，那句話要對斷電也成立才有用。
bool write_durable_impl(const std::string &path, const std::string &content) {
    const std::string tmp = path + ".tmp";
    Fd fd{::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)};
    if (!fd.valid()) return false;

    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t n =
            ::write(fd.get(), content.data() + written, content.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            fd.reset();
            ::unlink(tmp.c_str());
            return false;
        }
        written += static_cast<std::size_t>(n);
    }

    // ★ 先 fsync 再 rename。反過來的話 rename 生效了但內容還沒落地，
    //   斷電就留下一個「存在但是空的」exit 檔——看起來像有結論。
    while (::fsync(fd.get()) != 0) {
        if (errno == EINTR) continue;
        fd.reset();
        ::unlink(tmp.c_str());
        return false;
    }
    fd.reset();

    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }

    // ★ 還要 fsync 目錄，rename 才算真的落地。fsync 檔案**不會**順便保證
    //   目錄項目——少了這一步，斷電後 rename 可能整個沒發生。
    //   ⚠ 這裡失敗不回報 false：檔案內容已經落地、rename 也回成功了，
    //     此刻的結論是「已寫入」。目錄同步失敗只縮小斷電保證，
    //     謊稱整個寫入失敗會更糟（呼叫端可能因此重跑一次已經發生的作用）。
    Fd dir{::open(dirname_of(path).c_str(), O_RDONLY)};
    if (dir.valid()) {
        while (::fsync(dir.get()) != 0) {
            if (errno == EINTR) continue;
            break;
        }
    }
    return true;
}

// 起不來的 errno 對到 126 還是 127。
// errno 0 是「不是系統呼叫失敗」（例如 env 檔 source 不起來），歸 126。
int launch_number(int err) {
    return (err == ENOENT || err == ENOTDIR) ? 127 : 126;
}

// 把 vector<string> 變成 execve 要的 char*[]，尾端補 nullptr。
// ★ 回傳的指標指進 owner 裡，所以 owner 一定要活得比它久。
std::vector<char *> as_argv(std::vector<std::string> &owner) {
    std::vector<char *> out;
    out.reserve(owner.size() + 1);
    for (std::string &s : owner) out.push_back(s.data());
    out.push_back(nullptr);
    return out;
}

// ── 環境 ────────────────────────────────────────────────────────────
//
// ★★ 預設是一份**乾淨**的環境，不是繼承我們自己的。
//
// 繼承的話，同一個 Call 在不同的呼叫端手上會跑出不同結果（誰的 shell 剛好
// export 了什麼就影響誰），而那對一個專門產生證據的東西是致命的——證據會變成
// 「在某台機器某個 shell 底下」才成立。乾淨的預設讓一次呼叫的結果只取決於
// Call 本身。
//
// 裡面刻意只有 PATH：
//   * argv[0] 一定是絕對路徑，所以我們自己不需要 PATH；放它是因為子行程可能
//     會自己去叫別的程式，PATH 全空會壞掉一大片。
//   * 沒有 LC_ALL／LANG —— 什麼都不設的時候 POSIX 的預設就是 C locale，
//     所以「不設」本身就已經是可重現的了，不必多此一舉。
//   * 沒有 HOME、TZ、USER、SHELL。需要就自己寫進 env 檔。
std::vector<std::string> clean_environment() {
    return {"PATH=/usr/local/bin:/usr/bin:/bin"};
}

// ── source env 檔 ───────────────────────────────────────────────────
//
// ★ 做法是「開一個短命的 sh 去 source 那個檔、把結果環境倒出來，我們拿到之後
//   自己 execve 目標程式」，而不是 `sh -c '. env; exec prog'`。
//
// 差別在後者會讓 **shell** 成為 exec 目標程式的人：exec 失敗時是 shell 回 127，
// 跟「程式自己回 127」再次分不開——那正是 self-pipe 好不容易解決的事。
// 現在的做法讓 execve 還是由我們直接發出，所以 LaunchError 的區分完整保留，
// argv 邊界也不會被 shell 再解析一次。
//
// ⚠ 代價：只有**環境變數**帶得過去。ulimit、umask、fd 設定、shell 函式都不會，
//   因為那些是那個短命 sh 自己的行程狀態，它一結束就沒了。要那些就得讓 shell
//   來 exec，那就得放棄上面那個區分——是個真的二選一。
//
// ★ 那個 sh 自己也是從**乾淨環境**起跑的，所以：
//
//       沒給 env 檔  =>  乾淨環境
//       給了 env 檔  =>  乾淨環境 ⊕ 檔案改的部分
//
//   兩條路徑同一個基底，所以 env 檔裡寫 `export PATH="$PATH:/opt/bin"` 的結果
//   是可預測的。

// 倒完環境之後印的哨兵。
//
// ★ 為什麼需要它：如果 env 檔裡有一句 `exit 0`，那個 sh 會在 `env -0` 之前就
//   結束，於是「退出碼 0 ＋ 輸出是空的」。少了哨兵的話我們會把空輸出當成
//   「環境是空的」，然後拿一份空環境去跑程式——安靜且危險。
//   有哨兵就變成：沒看到哨兵 = 這份環境不完整 = 不要用。
constexpr const char *kEnvSentinel = "AOS_ENV_DUMP_COMPLETE";

// `. "$1" >&2` 把 env 檔自己印的東西導到 stderr，才不會混進要 parse 的環境傾印裡。
constexpr const char *kEnvScript =
    ". \"$1\" >&2 || exit 1\n"
    "env -0\n"
    "printf '%s\\0' \"$2\"\n";

struct Sourced {
    bool ok = false;
    std::string reason;
    std::vector<std::string> entries;  // "KEY=VALUE"
};

Sourced source_env_file(const std::string &env_path, int err_fd) {
    Sourced result;

    int out[2];
    if (::pipe(out) != 0) {
        result.reason = std::string{"source env：開不了管線："} + std::strerror(errno);
        return result;
    }
    Fd out_read{out[0]};
    Fd out_write{out[1]};

    // env 檔的 stdin 走 /dev/null，不給它碰這次呼叫的 stdin——那些位元組是
    // 要給目標程式的，被 source 的檔吃掉一段會非常難查。
    Fd devnull{::open("/dev/null", O_RDONLY)};
    if (!devnull.valid()) {
        result.reason = std::string{"source env：開不了 /dev/null："} + std::strerror(errno);
        return result;
    }

    std::vector<std::string> helper_argv = {"sh", "-c", kEnvScript, "sh", env_path,
                                            kEnvSentinel};
    std::vector<char *> cargv = as_argv(helper_argv);
    std::vector<std::string> base = clean_environment();
    std::vector<char *> cenvp = as_argv(base);

    ::fflush(nullptr);  // 理由見 spawn_and_wait
    const pid_t pid = ::fork();
    if (pid < 0) {
        result.reason = std::string{"source env：fork 失敗："} + std::strerror(errno);
        return result;
    }

    if (pid == 0) {
        // ── 子行程：那個短命的 sh ──
        if (::dup2(devnull.get(), STDIN_FILENO) >= 0 &&
            ::dup2(out_write.get(), STDOUT_FILENO) >= 0 &&
            ::dup2(err_fd, STDERR_FILENO) >= 0) {
            ::signal(SIGPIPE, SIG_DFL);
            ::execve("/bin/sh", cargv.data(), cenvp.data());
        }
        ::_exit(127);
    }

    // ── 父行程 ──
    out_write.reset();  // ★ 一定要關，否則永遠讀不到 EOF

    // ★ 先讀到 EOF 再 waitpid，不能反過來。環境傾印可能比管線 buffer 大
    //   （64 KiB），先 wait 的話對方寫滿就卡住、我們也在等它結束 —— 互鎖。
    std::string dump;
    char chunk[8192];
    bool read_failed = false;
    for (;;) {
        const ssize_t n = ::read(out_read.get(), chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            result.reason = std::string{"source env：讀不到環境傾印："} + std::strerror(errno);
            read_failed = true;
            break;
        }
        if (n == 0) break;
        dump.append(chunk, static_cast<std::size_t>(n));
    }

    int raw = 0;
    while (::waitpid(pid, &raw, 0) < 0) {
        if (errno != EINTR) break;
    }
    if (read_failed) return result;

    if (!WIFEXITED(raw) || WEXITSTATUS(raw) != 0) {
        result.reason = "source env：" + env_path + " 沒有成功 source（sh 的訊息在 stderr）";
        return result;
    }

    // 拆成 NUL 分隔的項目。
    std::vector<std::string> items;
    std::size_t start = 0;
    while (start < dump.size()) {
        const std::size_t stop = dump.find('\0', start);
        if (stop == std::string::npos) break;  // 沒有結尾 NUL 的殘塊，丟掉
        items.emplace_back(dump, start, stop - start);
        start = stop + 1;
    }

    if (items.empty() || items.back() != kEnvSentinel) {
        result.reason = "source env：" + env_path +
                        " 的環境傾印不完整（多半是檔案裡提早 exit 了）";
        return result;
    }
    items.pop_back();  // 哨兵不是環境變數

    for (std::string &item : items) {
        // env -0 的每一項一定是 KEY=VALUE。不是的話就不是我們認得的東西。
        if (item.find('=') == std::string::npos) continue;
        result.entries.push_back(std::move(item));
    }
    result.ok = true;
    return result;
}

// ── PATH 查找 ───────────────────────────────────────────────────────
//
// ★ 自己做，不用 execvp／execvpe，理由是**要用哪一份 PATH**。
//
// execvp 查找時用的是呼叫端行程 environ 裡的 PATH——那是我們自己的環境，
// 不是這次呼叫的環境。而這裡整套設計就是要讓一次呼叫的結果只取決於 Call，
// 所以查找一定要用「這次呼叫最終那份 envp」裡的 PATH（乾淨預設，或 env 檔說的）。
//
// 有了這個，argv[0] 就不必訂死絕對路徑也還是可重現的：PATH 不再來自呼叫端的
// shell，而是 aos 自己決定的。
//
// errno 照 POSIX 的規矩收斂：過程中只要有人回 EACCES 就報 EACCES（-> 126，
// 「找得到但起不來」），全程都是 ENOENT 才報 ENOENT（-> 127，「找不到」）。

// ⚠ 這個函式是在 fork 之後的子行程裡跑的，所以**不可以配置記憶體**。
//   所有緩衝區都由呼叫端在 fork 之前備好。
void exec_with_lookup(char *const *cargv, char *const *cenvp, char *scratch,
                      std::size_t scratch_size) {
    const char *name = cargv[0];

    // 含 '/' 就是路徑，不查找（POSIX execvp 的規矩）。
    for (const char *p = name; *p != '\0'; ++p) {
        if (*p == '/') {
            ::execve(name, cargv, cenvp);
            return;  // 失敗，errno 已經設好
        }
    }

    const char *path = nullptr;
    for (char *const *e = cenvp; *e != nullptr; ++e) {
        if (std::strncmp(*e, "PATH=", 5) == 0) {
            path = *e + 5;
            break;
        }
    }
    if (path == nullptr) {
        // 沒有 PATH 就沒得查。★ 刻意不退回系統預設：那會讓結果又取決於這台機器。
        errno = ENOENT;
        return;
    }

    const std::size_t name_len = std::strlen(name);
    int best = ENOENT;

    const char *segment = path;
    for (;;) {
        const char *end = segment;
        while (*end != '\0' && *end != ':') ++end;

        // 空的一段照 execvp 的慣例代表目前目錄。
        const char *dir = segment;
        std::size_t dir_len = static_cast<std::size_t>(end - segment);
        if (dir_len == 0) {
            dir = ".";
            dir_len = 1;
        }

        if (dir_len + 1 + name_len + 1 <= scratch_size) {
            std::memcpy(scratch, dir, dir_len);
            scratch[dir_len] = '/';
            std::memcpy(scratch + dir_len + 1, name, name_len);
            scratch[dir_len + 1 + name_len] = '\0';

            ::execve(scratch, cargv, cenvp);
            if (errno == EACCES) best = EACCES;
        }

        if (*end == '\0') break;
        segment = end + 1;
    }
    errno = best;
}

// ── 跑目標程式 ──────────────────────────────────────────────────────

struct Spawned {
    bool launched = false;  // exec 真的成功了嗎
    int launch_errno = 0;
    int raw_status = 0;
    bool timed_out = false;
    bool output_capped = false;
};

long long fd_size(int fd) {
    struct stat st;
    if (::fstat(fd, &st) != 0) return -1;
    return static_cast<long long>(st.st_size);
}

// ★★ 砍**行程群組**，不是砍那一個 pid。
//
// 子行程自己再開的孫行程不會因為 parent 被砍就跟著死——它們會被 init 收養，
// 然後在背景一直跑下去。這種孤兒最難查：工作「逾時被砍了」，但機器上還有一堆
// 東西在動。所以子行程一 fork 就 setpgid 自成一群，這裡對整群下手。
void kill_group(pid_t pgid, int sig) { ::kill(-pgid, sig); }

// 有限制的等法：輪詢 waitpid(WNOHANG)，順便看輸出大小。
// 到期就 SIGTERM -> 等寬限 -> SIGKILL。
void wait_with_limits(pid_t pid, const ExecOptions &limits, int out_fd, int err_fd,
                      Spawned &result) {
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();
    const bool has_deadline = limits.timeout.count() > 0;
    const bool has_cap = limits.max_output_bytes > 0;

    bool killing = false;
    clock::time_point kill_started;
    bool sent_kill = false;

    // ★ 從 1ms 開始、退到 20ms。短命的命令幾乎立刻收到，長命的不會一直空轉。
    auto nap = std::chrono::microseconds(1000);
    const auto nap_max = std::chrono::microseconds(20000);

    for (;;) {
        int raw = 0;
        const pid_t got = ::waitpid(pid, &raw, WNOHANG);
        if (got == pid) {
            result.raw_status = raw;
            return;
        }
        if (got < 0 && errno != EINTR) {
            result.raw_status = 0;
            return;
        }

        const auto now = clock::now();
        if (!killing) {
            if (has_deadline && now - started >= limits.timeout) {
                result.timed_out = true;
                killing = true;
            } else if (has_cap && fd_size(out_fd) + fd_size(err_fd) > limits.max_output_bytes) {
                result.output_capped = true;
                killing = true;
            }
            if (killing) {
                kill_started = now;
                kill_group(pid, SIGTERM);
            }
        } else if (!sent_kill && now - kill_started >= limits.grace) {
            // 寬限用完還沒走，來硬的。
            kill_group(pid, SIGKILL);
            sent_kill = true;
        }

        ::usleep(static_cast<useconds_t>(nap.count()));
        if (nap < nap_max) nap *= 2;
    }
}

// ★ fork 之後、exec 之前只能呼叫 async-signal-safe 的東西（dup2／chdir／execve／
//   write／_exit 都是）。所以 argv／envp 這種要配置記憶體的事全部在 fork 之前做完。
Spawned spawn_and_wait(const Call &call, const ExecOptions &limits,
                       std::vector<std::string> &envp, int in_fd, int out_fd,
                       int err_fd) {
    Spawned result;

    std::vector<std::string> argv_owner = call.argv;
    std::vector<char *> cargv = as_argv(argv_owner);
    std::vector<char *> cenvp = as_argv(envp);

    // PATH 查找要在子行程裡拼路徑，而那裡不能配置記憶體，所以緩衝先備好。
    std::vector<char> scratch(4096);

    int report[2];
    if (::pipe(report) != 0) {
        result.launch_errno = errno;
        return result;
    }
    Fd report_read{report[0]};
    Fd report_write{report[1]};

    if (::fcntl(report_write.get(), F_SETFD, FD_CLOEXEC) != 0) {
        result.launch_errno = errno;
        return result;
    }

    // ★★ fork 之前一定要把自己的 stdio buffer 沖乾淨。
    //
    // 子行程會拿到 parent 那些**還沒寫出去**的 stdio buffer 的副本，而它下一步
    // 就是 dup2 把 fd 1／2 指到證據檔。所以只要子行程那邊有任何東西沖了 stdio，
    // parent 積著的位元組就會落進**這次呼叫的 stdout／stderr 檔**，看起來像是
    // 子行程印的。對一個專門產生證據的東西來說，這是會污染證據的。
    //
    // 下面的 _exit(127) 本身不沖 stdio，所以一般建置看不到這件事。實際咬到的是
    // sanitizer 建置：ASan／TSan 會攔截 _exit，它們的 teardown 會沖。同一類的
    // 未來地雷還有：有人把 _exit 改成 exit、有人在子行程裡加一句 perror、
    // 連進來的某個函式庫裝了 atexit handler。
    //
    // 與其一個一個守，不如在源頭把 buffer 清空。fflush(nullptr) 沖掉所有輸出
    // 串流，這是 fork 前的標準動作。
    //
    // 前提是 parent 的 stdout 得是**全緩衝**的（管線、檔案）才會有殘留；
    // 終端機是行緩衝，所以手動跑很可能看不出來——這也是它難查的原因。
    ::fflush(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        result.launch_errno = errno;
        return result;
    }

    if (pid == 0) {
        // ── 子行程 ──
        const int report_fd = report_write.get();
        int failure = 0;
        // ★ 自成一個行程群組，這樣逾時要砍的時候連孫行程一起砍得掉。
        //   父行程那邊也會做一次（見下），兩邊都做是標準寫法：
        //   誰先跑到都算數，中間沒有窗口。
        ::setpgid(0, 0);
        if (::dup2(in_fd, STDIN_FILENO) < 0) {
            failure = errno;
        } else if (::dup2(out_fd, STDOUT_FILENO) < 0) {
            failure = errno;
        } else if (::dup2(err_fd, STDERR_FILENO) < 0) {
            failure = errno;
        } else if (::chdir(call.cwd.c_str()) != 0) {
            failure = errno;
        } else {
            // 父行程可能把 SIGPIPE 設成 SIG_IGN，那會被 exec 繼承下去。
            // 子行程應該拿到乾淨的預設處置，否則它寫進斷掉的管線時行為會跟
            // 在 shell 裡不一樣。
            ::signal(SIGPIPE, SIG_DFL);
            exec_with_lookup(cargv.data(), cenvp.data(), scratch.data(), scratch.size());
            failure = errno;
        }
        const ssize_t ignored = ::write(report_fd, &failure, sizeof failure);
        static_cast<void>(ignored);
        ::_exit(127);
    }

    // ── 父行程 ──
    ::setpgid(pid, pid);   // ★ 跟子行程那邊搶著做，避免競態
    report_write.reset();  // ★ 一定要關，否則永遠讀不到 EOF

    int child_errno = 0;
    ssize_t got = 0;
    for (;;) {
        got = ::read(report_read.get(), &child_errno, sizeof child_errno);
        if (got < 0 && errno == EINTR) continue;
        break;
    }

    // ★ 沒有任何限制的話走阻塞 waitpid：零輪詢成本。
    //   「不用逾時」的人不必為這個功能付任何代價。
    if (limits.timeout.count() <= 0 && limits.max_output_bytes <= 0) {
        int raw = 0;
        while (::waitpid(pid, &raw, 0) < 0) {
            if (errno != EINTR) break;
        }
        result.raw_status = raw;
    } else {
        wait_with_limits(pid, limits, out_fd, err_fd, result);
    }

    if (got == static_cast<ssize_t>(sizeof child_errno)) {
        result.launch_errno = child_errno;
        return result;  // launched 仍然是 false
    }
    result.launched = true;
    return result;
}

}  // namespace

bool write_durable(const std::string &path, const std::string &content) {
    return write_durable_impl(path, content);
}

std::vector<std::string> default_environment() { return clean_environment(); }

int exit_number(const Outcome &outcome) {
    // ★ 被我們自己砍掉的先判，否則它們會落成一般的 128+SIGKILL(=137)，
    //   跟「外面有人砍它」分不開。
    if (outcome.timed_out) return 124;       // timeout(1) 的慣例
    if (outcome.output_capped) return 123;   // ⚠ 沒有標準，我們自己定的
    switch (outcome.status) {
        case Status::Exited: return outcome.code;
        case Status::Signalled: return 128 + outcome.signal;
        case Status::LaunchError: return launch_number(outcome.err);
        case Status::Rejected: return 125;
        case Status::Unknown: return -1;  // 不寫檔
    }
    return -1;
}

ExecExecutor::ExecExecutor(Policy policy) : policy_(std::move(policy)) {}

Outcome ExecExecutor::run(const Call &call) {
    // 1. 純驗證。沒過就什麼都不做，只留下一個 125。
    const Check check = validate(call);
    if (!check) {
        const Outcome outcome = Outcome::rejected(check.reason);
        // ⚠ 這裡刻意不管寫檔成不成功：exit_path 本來就可能是壞的（它可能正是
        //   被拒絕的原因）。結論以回傳值為準。
        write_durable_impl(call.exit_path, std::to_string(exit_number(outcome)) + "\n");
        return outcome;
    }

    // 2. ★ 清掉上一次的結論。因為輸出是截斷覆寫的，不先清的話「exit 檔在」
    //    就不再代表「這一次跑完了」。
    ::unlink(call.exit_path.c_str());

    // 3. 第一個真正的外部作用。
    //    ★ 開檔要排在 source 之前：這樣 env 檔的雜訊和 sh 的錯誤訊息才有地方去
    //      （進這次呼叫的 stderr 證據檔），而不是漏到我們自己的 stderr。
    Fd in{::open(call.stdin_path.c_str(), O_RDONLY)};
    Fd out;
    Fd err;
    int open_errno = 0;
    const char *open_what = nullptr;

    if (!in.valid()) {
        open_errno = errno;
        open_what = "stdin";
    } else {
        out.reset(::open(call.stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600));
        if (!out.valid()) {
            open_errno = errno;
            open_what = "stdout";
        } else {
            err.reset(::open(call.stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600));
            if (!err.valid()) {
                open_errno = errno;
                open_what = "stderr";
            }
        }
    }

    Outcome outcome;
    if (open_what != nullptr) {
        outcome = Outcome::launch_error(
            open_errno, std::string{"開不了 "} + open_what + "：" + std::strerror(open_errno));
    } else {
        // 4. 環境。沒給 env 檔就是乾淨的那份；給了就 source 完拿結果。
        std::vector<std::string> envp = clean_environment();
        bool env_ok = true;
        if (call.env) {
            Sourced sourced = source_env_file(*call.env, err.get());
            if (!sourced.ok) {
                // 目標程式一個位元組都還沒跑到。errno 0 -> exit 檔是 126。
                outcome = Outcome::launch_error(0, sourced.reason);
                env_ok = false;
            } else {
                envp = std::move(sourced.entries);
            }
        }

        if (env_ok) {
            // 5-6. 跑，並等它。限制由 policy 決定（不給就是都不限）。
            const ExecOptions limits = policy_ ? policy_(call) : ExecOptions{};
            const Spawned spawned =
                spawn_and_wait(call, limits, envp, in.get(), out.get(), err.get());
            if (!spawned.launched) {
                outcome = Outcome::launch_error(spawned.launch_errno,
                                                std::strerror(spawned.launch_errno));
            } else if (WIFSIGNALED(spawned.raw_status)) {
                // ★ 旗標要在這裡帶上：被砍是事實（Signalled），
                //   「是我們砍的、為什麼砍」是額外資訊，兩者都要留。
                // ★ 這一格就是要拿原始 status 的理由。只看合成數字的話，
                //   這裡跟「程式自己 exit(128+S)」分不開。
                outcome = Outcome::signalled(WTERMSIG(spawned.raw_status));
                outcome.timed_out = spawned.timed_out;
                outcome.output_capped = spawned.output_capped;
            } else if (WIFEXITED(spawned.raw_status)) {
                // ⚠ 也可能是它收到 SIGTERM 之後自己乾淨地 exit 了——
                //   那仍然是逾時造成的，旗標一樣要帶。
                outcome = Outcome::exited(WEXITSTATUS(spawned.raw_status));
                outcome.timed_out = spawned.timed_out;
                outcome.output_capped = spawned.output_capped;
            } else {
                // 既沒 exit 也沒被信號殺死（stopped／continued，我們沒有 WUNTRACED
                // 所以理論上到不了這裡）。不猜。
                outcome = Outcome::unknown("waitpid 回了一個既非 exited 也非 signalled 的 status");
            }
        }
    }

    in.reset();
    out.reset();
    err.reset();

    // 7. 有結論才寫。Unknown 不寫——檔案不在才代表「還不知道」。
    const int number = exit_number(outcome);
    if (number >= 0) {
        if (!write_durable_impl(call.exit_path, std::to_string(number) + "\n")) {
            // 跑是真的跑了，但結論落不了地。對讀檔的人來說這就是「不知道」，
            // 所以回傳值也要說實話，不能假裝寫成功了。
            return Outcome::unknown("跑完了，但 exit 檔寫不進去：" + call.exit_path +
                                    "（實際結論是 " + to_string(outcome.status) + "）");
        }
    }
    return outcome;
}

}  // namespace aossimple
