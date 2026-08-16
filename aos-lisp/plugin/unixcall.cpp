// unixcall.cpp — 把一次 Unix 呼叫變成一個可以求值的 list。
//
//     (unix/call :argv   ["/bin/ls" "-la" "/tmp"]
//                :cwd    "/home/me"
//                :stdin  "/run/aos/42/stdin"
//                :stdout "/run/aos/42/stdout"
//                :stderr "/run/aos/42/stderr"
//                :status "/run/aos/42/status"
//                :caller "agent-7")          ; 可選，不透明標籤
//
// ── ★★ 這個檔為什麼在 C++ 而不是 Janet ──────────────────────────────
//
// 因為 Janet 分不出「程式自己 exit(137)」和「被 SIGKILL 殺死」：
//
//     sh -c 'exit 137'    os/proc-wait => 137
//     sh -c 'kill -9 $$'  os/proc-wait => 137
//
// core/process 身上只有 :return-code，原始的 wait status 在 Janet 這層就是拿不到，
// 所以純 Janet 版被迫發明一個 :ambiguous 狀態來誠實地說「我不知道」。
//
// waitpid 的原始 status 分得清清楚楚：
//
//     exit 137     raw=0x8900  WIFEXITED=1  WEXITSTATUS=137
//     kill -9      raw=0x0009  WIFSIGNALED=1 WTERMSIG=9
//
// 所以這一版沒有 :ambiguous 這個狀態，只有 :exited / :signalled / :launch-error，
// 三個都是確定的。
//
// 順便還修好第二件 Janet 做不到的事：**分辨 exec 失敗和「程式跑了但回 127」**。
// 做法是經典的 self-pipe——子行程在 exec 之前開一條 CLOEXEC 的管線，exec 成功
// 的話管線自動關閉、父行程讀到 EOF；exec 失敗就把 errno 寫進去。沒有這個機制
// 的話「執行檔不存在」和「shell 回 127」長得一模一樣。
//
// ── 為什麼三條串流走檔案 ────────────────────────────────────────────
//
// loop 是一次跑一個、全程阻塞的。如果三條串流是管線，一個輸出量大的子行程會塞滿
// pipe buffer 然後跟我們互等，整條 loop 就停在那裡。走檔案的話這一整類問題不存在，
// 所以下面可以老實 fork 完就 waitpid，不必開讀取執行緒。
// （aos-core 的 CLI 要開兩條執行緒、daemon 端 stdin 要做成拉的，就是在付這個代價。）
//
// ── status 檔為什麼不是「一個裝著整數的檔案」──────────────────────
//
// 整數表達不了 launch-error 和 signalled，而且「檔案不存在」要能可靠地代表
// 「還不知道」，就必須讓寫入是原子的。所以：內容是一張 Janet table 字面值
// （parse 讀得回來），寫法是先寫 <status>.tmp 再 rename。
// 檔案在 = 結果已知；檔案不在 = 還不知道，**不可以當成失敗**。

#include "unixcall.hpp"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

extern char **environ;

namespace aoslisp {
namespace {

// ── 取參數 ──────────────────────────────────────────────────────────

struct Call {
    std::vector<std::string> argv;
    std::string cwd, stdin_path, stdout_path, stderr_path, status_path;
    bool has_caller = false;
    std::string caller;
    bool has_env = false;
    std::vector<std::string> envp;  // "K=V"
};

bool clean(const std::string &s) { return s.find('\0') == std::string::npos; }

std::string get_string(Janet v, const char *what) {
    if (!janet_checktype(v, JANET_STRING) && !janet_checktype(v, JANET_BUFFER)) {
        janet_panicf("%s 要是字串，拿到 %v", what, v);
    }
    JanetByteView bytes = janet_getbytes(&v, 0);
    std::string s(reinterpret_cast<const char *>(bytes.bytes),
                  static_cast<std::size_t>(bytes.len));
    if (!clean(s)) janet_panicf("%s 不能含 NUL", what);
    return s;
}

void need_absolute(const std::string &s, const char *what) {
    if (s.empty() || s[0] != '/') {
        janet_panicf("%s 要是絕對路徑，拿到 %s", what, s.c_str());
    }
}

// argv[0] 一定要絕對路徑，而且不走 shell、不走 PATH：走 PATH 的話「實際跑了哪支
// 執行檔」取決於當下的環境變數，那不是證據。要 PATH 查找就自己先查好再送進來。
Call parse_args(int32_t argc, Janet *argv) {
    if (argc % 2 != 0) janet_panic("參數要成對：(unix/call :key value …)");

    Call call;
    bool saw[6] = {false, false, false, false, false, false};
    const char *names[6] = {":argv", ":cwd", ":stdin", ":stdout", ":stderr", ":status"};

    for (int32_t i = 0; i + 1 < argc; i += 2) {
        if (!janet_checktype(argv[i], JANET_KEYWORD)) {
            janet_panicf("key 要是 keyword，拿到 %v", argv[i]);
        }
        const char *key = reinterpret_cast<const char *>(janet_unwrap_keyword(argv[i]));
        Janet v = argv[i + 1];

        if (!strcmp(key, "argv")) {
            JanetView items = janet_getindexed(argv, i + 1);
            if (items.len == 0) janet_panic(":argv 不能是空的");
            for (int32_t k = 0; k < items.len; ++k) {
                call.argv.push_back(get_string(items.items[k], ":argv 的每一項"));
            }
            need_absolute(call.argv[0], ":argv[0]（不走 shell、不走 PATH）");
            saw[0] = true;
        } else if (!strcmp(key, "cwd")) {
            call.cwd = get_string(v, ":cwd");
            need_absolute(call.cwd, ":cwd");
            saw[1] = true;
        } else if (!strcmp(key, "stdin")) {
            call.stdin_path = get_string(v, ":stdin");
            need_absolute(call.stdin_path, ":stdin");
            saw[2] = true;
        } else if (!strcmp(key, "stdout")) {
            call.stdout_path = get_string(v, ":stdout");
            need_absolute(call.stdout_path, ":stdout");
            saw[3] = true;
        } else if (!strcmp(key, "stderr")) {
            call.stderr_path = get_string(v, ":stderr");
            need_absolute(call.stderr_path, ":stderr");
            saw[4] = true;
        } else if (!strcmp(key, "status")) {
            call.status_path = get_string(v, ":status");
            need_absolute(call.status_path, ":status");
            saw[5] = true;
        } else if (!strcmp(key, "caller")) {
            // 不透明標籤：不驗證、不 stat、不保證唯一。只回答「這次呼叫掛誰的名」。
            call.caller = get_string(v, ":caller");
            call.has_caller = true;
        } else if (!strcmp(key, "env")) {
            // 給了就是**完全換掉**（明確的環境政策）；沒給就繼承本行程的。
            const JanetKV *kvs = nullptr;
            int32_t len = 0, cap = 0;
            if (!janet_dictionary_view(v, &kvs, &len, &cap)) {
                janet_panicf(":env 要是 table 或 struct，拿到 %v", v);
            }
            for (int32_t k = 0; k < cap; ++k) {
                if (janet_checktype(kvs[k].key, JANET_NIL)) continue;
                call.envp.push_back(get_string(kvs[k].key, ":env 的 key") + "=" +
                                    get_string(kvs[k].value, ":env 的 value"));
            }
            call.has_env = true;
        } else {
            janet_panicf("不認得的參數 :%s", key);
        }
    }

    for (int i = 0; i < 6; ++i) {
        if (!saw[i]) janet_panicf("%s 一定要給", names[i]);
    }
    return call;
}

// ── 落地前檢查：所有會失敗的事都要在有任何外部作用之前發生 ──────────

int mode_of(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return static_cast<int>(st.st_mode & S_IFMT);
}

std::string dirname_of(const std::string &path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

void preflight(const Call &call) {
    if (mode_of(call.cwd) != S_IFDIR) {
        janet_panicf(":cwd 不是目錄：%s", call.cwd.c_str());
    }
    if (mode_of(call.stdin_path) < 0) {
        janet_panicf(":stdin 檔案不存在：%s", call.stdin_path.c_str());
    }

    const std::string *targets[3] = {&call.stdout_path, &call.stderr_path,
                                     &call.status_path};
    const char *labels[3] = {":stdout", ":stderr", ":status"};
    for (int i = 0; i < 3; ++i) {
        // 已經存在的**普通檔案**要拒絕——那是上一次呼叫留下的證據，靜靜蓋掉的話
        // 就分不出「這次寫的」和「上次剩的」了。/dev/null 之類的裝置檔不受限制，
        // 而且這裡看的是 mode 不是比對路徑字串，所以不必為 /dev/null 開特例。
        if (mode_of(*targets[i]) == S_IFREG) {
            janet_panicf("%s 已經有一個普通檔案了，不覆蓋既有證據：%s", labels[i],
                         targets[i]->c_str());
        }
        if (mode_of(dirname_of(*targets[i])) != S_IFDIR) {
            janet_panicf("%s 的所在目錄不存在：%s", labels[i],
                         dirname_of(*targets[i]).c_str());
        }
    }
}

// ── 執行 ────────────────────────────────────────────────────────────

struct Outcome {
    enum Kind { Exited, Signalled, LaunchError } kind = Exited;
    int code = 0;
    int signal = 0;
    bool core_dumped = false;
    int err = 0;  // launch error 的 errno
    pid_t pid = 0;
};

void close_if(int fd) {
    if (fd >= 0) close(fd);
}

// ★ fork 之後、exec 之前只能呼叫 async-signal-safe 的東西（dup2／chdir／execve／
//   write／_exit 都是）。所以 argv／envp 這些要配置記憶體的事全部在 fork 之前做完。
Outcome spawn_and_wait(const Call &call, int in_fd, int out_fd, int err_fd) {
    Outcome result;

    std::vector<char *> cargv;
    cargv.reserve(call.argv.size() + 1);
    for (const std::string &a : call.argv) cargv.push_back(const_cast<char *>(a.c_str()));
    cargv.push_back(nullptr);

    std::vector<char *> cenvp;
    if (call.has_env) {
        cenvp.reserve(call.envp.size() + 1);
        for (const std::string &e : call.envp) cenvp.push_back(const_cast<char *>(e.c_str()));
        cenvp.push_back(nullptr);
    }
    char **envp = call.has_env ? cenvp.data() : environ;

    // self-pipe：exec 成功的話寫端因為 CLOEXEC 自動關閉，父行程讀到 EOF；
    // exec 失敗就把 errno 寫進去。沒有這個的話「執行檔不存在」跟「程式回 127」
    // 分不出來。
    int report[2];
    if (pipe(report) != 0) {
        result.kind = Outcome::LaunchError;
        result.err = errno;
        return result;
    }
    if (fcntl(report[1], F_SETFD, FD_CLOEXEC) != 0) {
        close(report[0]);
        close(report[1]);
        result.kind = Outcome::LaunchError;
        result.err = errno;
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(report[0]);
        close(report[1]);
        result.kind = Outcome::LaunchError;
        result.err = errno;
        return result;
    }

    if (pid == 0) {
        // ── 子行程 ──
        close(report[0]);
        if (dup2(in_fd, STDIN_FILENO) < 0) goto child_failed;
        if (dup2(out_fd, STDOUT_FILENO) < 0) goto child_failed;
        if (dup2(err_fd, STDERR_FILENO) < 0) goto child_failed;
        if (chdir(call.cwd.c_str()) != 0) goto child_failed;
        // aos-core 把 SIGPIPE 設成 SIG_IGN，那會被 exec 繼承下去。子行程應該
        // 拿到乾淨的預設處置，否則它寫進斷掉的管線時行為會跟在 shell 裡不一樣。
        signal(SIGPIPE, SIG_DFL);
        execve(cargv[0], cargv.data(), envp);
    child_failed : {
        const int saved = errno;
        ssize_t ignored = write(report[1], &saved, sizeof saved);
        (void)ignored;
        _exit(127);
    }
    }

    // ── 父行程 ──
    close(report[1]);
    int child_errno = 0;
    ssize_t got = read(report[0], &child_errno, sizeof child_errno);
    close(report[0]);

    int raw = 0;
    while (waitpid(pid, &raw, 0) < 0 && errno == EINTR) {
        /* 被信號打斷就重試 */
    }

    if (got == static_cast<ssize_t>(sizeof child_errno)) {
        result.kind = Outcome::LaunchError;
        result.err = child_errno;
        return result;
    }

    result.pid = pid;
    if (WIFSIGNALED(raw)) {
        result.kind = Outcome::Signalled;
        result.signal = WTERMSIG(raw);
#ifdef WCOREDUMP
        result.core_dumped = WCOREDUMP(raw) != 0;
#endif
    } else {
        result.kind = Outcome::Exited;
        result.code = WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
    }
    return result;
}

// ── status 檔：原子寫入 ─────────────────────────────────────────────

long long size_of(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return static_cast<long long>(st.st_size);
}

// ⚠ 這擋得住「讀到半個檔」，擋不住斷電：這裡沒有 fsync，所以 rename 之後
//   資料還可能只在 page cache 裡。要斷電保證得再往下一層。
void write_atomic(const std::string &path, const std::string &content) {
    const std::string tmp = path + ".tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) janet_panicf("寫不了 %s：%s", tmp.c_str(), strerror(errno));
    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t n = write(fd, content.data() + written, content.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            const int saved = errno;
            close(fd);
            unlink(tmp.c_str());
            janet_panicf("寫 %s 失敗：%s", tmp.c_str(), strerror(saved));
        }
        written += static_cast<std::size_t>(n);
    }
    close(fd);
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        const int saved = errno;
        unlink(tmp.c_str());
        janet_panicf("rename 到 %s 失敗：%s", path.c_str(), strerror(saved));
    }
}

double monotonic_now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

Janet string_value(const std::string &s) {
    return janet_stringv(reinterpret_cast<const uint8_t *>(s.data()),
                         static_cast<int32_t>(s.size()));
}

// ── cfunctions ──────────────────────────────────────────────────────

Janet cfun_unix_call(int32_t argc, Janet *argv) {
    const Call call = parse_args(argc, argv);
    preflight(call);

    const time_t started = time(nullptr);
    const double t0 = monotonic_now();

    // 第一個外部作用。★ 這同時是一個免費的兩階段標記：status 檔不在的時候，
    //   stdout 檔在不在就分得出「根本沒起來」和「可能跑過了」——後者絕對不可以
    //   自動重跑。
    const int in_fd = open(call.stdin_path.c_str(), O_RDONLY);
    if (in_fd < 0) janet_panicf("開不了 :stdin %s：%s", call.stdin_path.c_str(), strerror(errno));
    const int out_fd = open(call.stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (out_fd < 0) {
        close(in_fd);
        janet_panicf("開不了 :stdout %s：%s", call.stdout_path.c_str(), strerror(errno));
    }
    const int err_fd = open(call.stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (err_fd < 0) {
        close(in_fd);
        close(out_fd);
        janet_panicf("開不了 :stderr %s：%s", call.stderr_path.c_str(), strerror(errno));
    }

    const Outcome outcome = spawn_and_wait(call, in_fd, out_fd, err_fd);

    close_if(in_fd);
    close_if(out_fd);
    close_if(err_fd);

    JanetTable *record = janet_table(12);
    switch (outcome.kind) {
        case Outcome::Exited:
            janet_table_put(record, janet_ckeywordv("status"), janet_ckeywordv("exited"));
            janet_table_put(record, janet_ckeywordv("code"), janet_wrap_integer(outcome.code));
            break;
        case Outcome::Signalled:
            // ★ 這一格就是搬來 C++ 的理由。Janet 只看得到 128+signal，
            //   跟「程式自己 exit 那個碼」混在一起。
            janet_table_put(record, janet_ckeywordv("status"), janet_ckeywordv("signalled"));
            janet_table_put(record, janet_ckeywordv("signal"), janet_wrap_integer(outcome.signal));
            janet_table_put(record, janet_ckeywordv("core-dumped"),
                            janet_wrap_boolean(outcome.core_dumped));
            break;
        case Outcome::LaunchError:
            janet_table_put(record, janet_ckeywordv("status"), janet_ckeywordv("launch-error"));
            janet_table_put(record, janet_ckeywordv("errno"), janet_wrap_integer(outcome.err));
            janet_table_put(record, janet_ckeywordv("message"),
                            string_value(strerror(outcome.err)));
            break;
    }
    if (outcome.pid != 0) {
        janet_table_put(record, janet_ckeywordv("pid"), janet_wrap_integer(outcome.pid));
    }

    JanetArray *argv_out = janet_array(static_cast<int32_t>(call.argv.size()));
    for (const std::string &a : call.argv) janet_array_push(argv_out, string_value(a));
    janet_table_put(record, janet_ckeywordv("argv"), janet_wrap_array(argv_out));
    janet_table_put(record, janet_ckeywordv("cwd"), string_value(call.cwd));
    if (call.has_caller) {
        janet_table_put(record, janet_ckeywordv("caller"), string_value(call.caller));
    }
    janet_table_put(record, janet_ckeywordv("started"),
                    janet_wrap_number(static_cast<double>(started)));
    janet_table_put(record, janet_ckeywordv("duration"),
                    janet_wrap_number(monotonic_now() - t0));
    janet_table_put(record, janet_ckeywordv("stdout-size"),
                    janet_wrap_number(static_cast<double>(size_of(call.stdout_path))));
    janet_table_put(record, janet_ckeywordv("stderr-size"),
                    janet_wrap_number(static_cast<double>(size_of(call.stderr_path))));

    const Janet value = janet_wrap_table(record);
    JanetBuffer *serialized = janet_buffer(256);
    janet_formatb(serialized, "%q\n", value);
    write_atomic(call.status_path,
                 std::string(reinterpret_cast<const char *>(serialized->data),
                             static_cast<std::size_t>(serialized->count)));
    return value;
}

Janet cfun_unix_read_status(int32_t argc, Janet *argv) {
    janet_fixarity(argc, 1);
    const std::string path = get_string(argv[0], "路徑");
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return janet_wrap_nil();  // 還不知道，不是失敗

    std::string content;
    char chunk[4096];
    for (;;) {
        const ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            janet_panicf("讀 %s 失敗：%s", path.c_str(), strerror(errno));
        }
        if (n == 0) break;
        content.append(chunk, static_cast<std::size_t>(n));
    }
    close(fd);

    // 交給 Janet 的 parser：格式就是一張 table 字面值。
    JanetParser parser;
    janet_parser_init(&parser);
    // parser 是一次吃一個 byte 的串流式介面，沒有整段餵的版本。
    for (unsigned char c : content) janet_parser_consume(&parser, c);
    janet_parser_eof(&parser);
    Janet parsed = janet_wrap_nil();
    if (janet_parser_status(&parser) == JANET_PARSE_ERROR) {
        const char *why = janet_parser_error(&parser);
        janet_parser_deinit(&parser);
        janet_panicf("%s 讀得出來但 parse 不了：%s", path.c_str(), why);
    }
    if (janet_parser_has_more(&parser)) parsed = janet_parser_produce(&parser);
    janet_parser_deinit(&parser);
    return parsed;
}

const JanetReg cfuns[] = {
    {"call", cfun_unix_call,
     "(unix/call :argv [..] :cwd .. :stdin .. :stdout .. :stderr .. :status .. "
     "[:caller ..] [:env {..}])\n\n"
     "跑一次 Unix 呼叫。三條串流走檔案，結論寫進 :status（原子寫入）並回傳。\n"
     ":status 的三個值都是確定的：:exited / :signalled / :launch-error。"},
    {"read-status", cfun_unix_read_status,
     "(unix/read-status path)\n\n"
     "讀回一個 status 檔。★ 檔案不存在回 nil——那是「還不知道」，不是失敗。"},
    {nullptr, nullptr, nullptr},
};

}  // namespace

// ★ janet_cfuns_prefix，不是 janet_cfuns。
//   janet_cfuns 的 regprefix 只用在 marshal 的登記表上，綁定名是光禿禿的 `call`；
//   要在 Janet 那邊叫得出 `unix/call` 得用 _prefix 這一版。
//   症狀是任務裡 `unknown symbol unix/call`，但 registry 裡其實有東西。
void install_unix_cfuns(JanetTable *env) { janet_cfuns_prefix(env, "unix", cfuns); }

}  // namespace aoslisp
