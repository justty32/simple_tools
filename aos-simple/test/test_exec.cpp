// exec 本身。★ 這支會真的起子行程、真的寫檔——跟 test_core 分開就是為了讓
// 「核心一個檔都不開」那句話保持是真的。

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <chrono>

#include "../src/queue.hpp"
#include "../src/exec.hpp"
#include "../src/pool.hpp"
#include "../src/router.hpp"
#include "check.hpp"

using namespace aossimple;

namespace {

std::string root;
int seq = 0;

std::string slurp(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "<檔案不存在>";
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void spit(const std::string &path, const std::string &content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

bool exists(const std::string &path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

// 一個乾淨的案例目錄，五個路徑都填好。
Call workspace(const std::string &stdin_content = "") {
    ++seq;
    const std::string dir = root + "/" + std::to_string(seq);
    ::mkdir(dir.c_str(), 0700);
    spit(dir + "/stdin", stdin_content);

    Call c;
    c.stdin_path = dir + "/stdin";
    c.stdout_path = dir + "/stdout";
    c.stderr_path = dir + "/stderr";
    c.exit_path = dir + "/exit";
    c.cwd = dir;
    return c;
}

std::string dir_of(const Call &c) { return c.cwd; }

void test_success() {
    check::section("跑成功");
    ExecExecutor exec;

    Call c = workspace();
    c.argv = {"/bin/sh", "-c", "echo out; echo err >&2; exit 0"};
    const Outcome o = exec.run(c);

    check::ok(o.status == Status::Exited, ":exited");
    check::eq(o.code, 0, "code 0");
    check::eq(slurp(c.stdout_path), std::string{"out\n"}, "stdout 落到指定的檔");
    check::eq(slurp(c.stderr_path), std::string{"err\n"}, "stderr 落到指定的檔");
    check::eq(slurp(c.exit_path), std::string{"0\n"}, "exit 檔就是一個數字");
    check::ok(!exists(c.exit_path + ".tmp"), "★ 原子寫入的 .tmp 沒留下");
}

void test_stdin_and_cwd() {
    check::section("stdin 與 cwd");
    ExecExecutor exec;

    Call c = workspace("餵進去的內容\n");
    c.argv = {"/bin/cat"};
    exec.run(c);
    check::eq(slurp(c.stdout_path), std::string{"餵進去的內容\n"},
              "★ stdin 從指定的檔讀進子行程");

    Call before;
    char buf[4096];
    const std::string cwd_before = ::getcwd(buf, sizeof buf) ? buf : "";

    Call p = workspace();
    p.argv = {"/bin/pwd"};
    exec.run(p);
    check::eq(slurp(p.stdout_path), dir_of(p) + "\n", "★ 子行程的 cwd 是指定的那個");

    const std::string cwd_after = ::getcwd(buf, sizeof buf) ? buf : "";
    check::eq(cwd_after, cwd_before,
              "★ 而且沒有改到自己的 cwd（chdir 在子行程裡，不是在我們這邊）");
}

void test_nonzero() {
    check::section("nonzero 是結果，不是錯誤");
    ExecExecutor exec;

    Call c = workspace();
    c.argv = {"/bin/sh", "-c", "exit 7"};
    const Outcome o = exec.run(c);
    check::ok(o.status == Status::Exited, "★ nonzero 也是 :exited");
    check::eq(o.code, 7, "code 7");
    check::eq(slurp(c.exit_path), std::string{"7\n"}, "exit 檔是 7");
}

void test_signal_vs_exit() {
    check::section("★★ exit 137 vs SIGKILL");
    ExecExecutor exec;

    Call a = workspace();
    a.argv = {"/bin/sh", "-c", "exit 137"};
    const Outcome ea = exec.run(a);

    Call b = workspace();
    b.argv = {"/bin/sh", "-c", "kill -9 $$"};
    const Outcome eb = exec.run(b);

    // ★ Outcome 分得開，因為它拿的是 waitpid 的原始 status。
    check::ok(ea.status == Status::Exited, "★ exit 137 是確定的 Exited");
    check::eq(ea.code, 137, "code 137");
    check::ok(eb.status == Status::Signalled, "★ SIGKILL 是 Signalled，不是 exit 137");
    check::eq(eb.signal, 9, "★ 而且說得出是 9 號信號");

    // ⚠ exit 檔裡那個數字分不開——這是「只寫一個數字」的固有代價，不是 bug。
    //    這一項守著這件事：哪天它們不一樣了，代表有人在猜。
    check::eq(slurp(a.exit_path), slurp(b.exit_path),
              "⚠ 但 exit 檔裡兩者都是同一個數字（一個數字的固有極限）");
    check::eq(slurp(a.exit_path), std::string{"137\n"}, "都是 137");

    Call term = workspace();
    term.argv = {"/bin/sh", "-c", "kill -15 $$"};
    const Outcome et = exec.run(term);
    check::eq(et.signal, 15, "SIGTERM 是 15");
    check::eq(slurp(term.exit_path), std::string{"143\n"}, "exit 檔是 128+15");
}

void test_launch_error() {
    check::section("★ 起不來 vs 程式回 127");
    ExecExecutor exec;

    Call missing = workspace();
    missing.argv = {"/no/such/binary-xyz"};
    const Outcome om = exec.run(missing);
    check::ok(om.status == Status::LaunchError, "★ 執行檔不存在是 LaunchError");
    check::eq(om.err, ENOENT, "errno 是 ENOENT");
    check::eq(slurp(missing.exit_path), std::string{"127\n"}, "exit 檔是 127");
    check::eq(slurp(missing.stdout_path), std::string{""},
              "stdout 檔建起來了但是空的");

    Call e127 = workspace();
    e127.argv = {"/bin/sh", "-c", "exit 127"};
    const Outcome o127 = exec.run(e127);
    check::ok(o127.status == Status::Exited,
              "★ 而「程式跑了但回 127」是 Exited——self-pipe 分得開");
    check::eq(o127.code, 127, "code 127");

    // 找得到但不能執行 -> 126
    Call noexec = workspace();
    const std::string data = dir_of(noexec) + "/not-executable";
    spit(data, "just data\n");
    noexec.argv = {data};
    const Outcome one = exec.run(noexec);
    check::ok(one.status == Status::LaunchError, "沒有執行權限也是 LaunchError");
    check::eq(slurp(noexec.exit_path), std::string{"126\n"},
              "★ 找得到但起不來是 126，跟找不到的 127 分得開");

    // cwd 不存在 -> 子行程 chdir 失敗 -> LaunchError
    Call badcwd = workspace();
    badcwd.argv = {"/bin/echo", "x"};
    badcwd.cwd = "/no/such/dir/at/all";
    const Outcome ob = exec.run(badcwd);
    check::ok(ob.status == Status::LaunchError, "cwd 不存在是 LaunchError");

    // stdin 不存在 -> 開不了 -> LaunchError
    Call badin = workspace();
    badin.argv = {"/bin/echo", "x"};
    badin.stdin_path = "/no/such/stdin";
    const Outcome oi = exec.run(badin);
    check::ok(oi.status == Status::LaunchError, "stdin 不存在是 LaunchError");
    check::ok(oi.reason.find("stdin") != std::string::npos, "而且說得出是哪一條");
}

void test_rejected() {
    check::section("Rejected：驗證在 Executor 裡");
    ExecExecutor exec;

    Call c = workspace();
    c.argv = {""};  // argv[0] 空字串
    const Outcome o = exec.run(c);

    check::ok(o.status == Status::Rejected, "★ 形狀不對是 Rejected");
    check::ok(o.reason.find("argv[0]") != std::string::npos, "說得出是哪個欄位");
    check::eq(slurp(c.exit_path), std::string{"125\n"},
              "★ 被拒絕也寫 exit 檔，等的人不會永遠等");
    check::ok(!exists(c.stdout_path),
              "★ 但 stdout 檔沒有被建出來——Rejected 保證什麼都沒發生");
}

void test_overwrite() {
    check::section("截斷覆寫");
    ExecExecutor exec;

    Call c = workspace();
    c.argv = {"/bin/sh", "-c", "echo second"};
    spit(c.stdout_path, "上一次留下的很長很長的內容\n");
    spit(c.exit_path, "99\n");

    const Outcome o = exec.run(c);
    check::ok(o.status == Status::Exited, "照跑，不因為檔案已存在就拒絕");
    check::eq(slurp(c.stdout_path), std::string{"second\n"},
              "★ stdout 被截斷覆寫，沒有殘留上一次的尾巴");
    check::eq(slurp(c.exit_path), std::string{"0\n"}, "exit 也是新的");
}

void test_unknown_marker() {
    check::section("★ exit 檔不在 = 還不知道");
    ExecExecutor exec;

    Call c = workspace();
    c.argv = {"/bin/echo", "x"};
    check::ok(!exists(c.exit_path), "★ 還沒跑：exit 檔不在");
    check::ok(!exists(c.stdout_path), "★ stdout 也不在 = 確定連開檔都還沒到");

    // 先放一個假的舊結論，證明 unlink 那一步真的有把它清掉。
    spit(c.exit_path, "42\n");
    check::ok(exists(c.exit_path), "放了一個上一次的結論");

    exec.run(c);
    check::eq(slurp(c.exit_path), std::string{"0\n"},
              "★ 跑完之後是這一次的結論，不是上一次剩的");
    check::ok(exists(c.stdout_path), "stdout 檔在");

    // Unknown 不寫檔
    Outcome u = Outcome::unknown("測試用");
    check::eq(exit_number(u), -1, "★ Unknown 的 exit_number 是 -1 = 不要寫檔");
}

void test_exit_number_mapping() {
    check::section("exit_number 的對應");
    check::eq(exit_number(Outcome::exited(0)), 0, "exited 0 -> 0");
    check::eq(exit_number(Outcome::exited(7)), 7, "exited 7 -> 7");
    check::eq(exit_number(Outcome::signalled(9)), 137, "signalled 9 -> 128+9");
    check::eq(exit_number(Outcome::signalled(15)), 143, "signalled 15 -> 128+15");
    check::eq(exit_number(Outcome::launch_error(ENOENT, "")), 127, "ENOENT -> 127");
    check::eq(exit_number(Outcome::launch_error(EACCES, "")), 126, "EACCES -> 126");
    check::eq(exit_number(Outcome::rejected("")), 125, "rejected -> 125");
    check::eq(exit_number(Outcome::unknown("")), -1, "unknown -> -1（不寫）");
}

void test_argv_fidelity() {
    check::section("參數邊界");
    ExecExecutor exec;

    Call c = workspace();
    // 含空白、引號、非 ASCII 的參數。★ 不經 shell，所以這些都不會被再解析一次。
    c.argv = {"/bin/sh", "-c", "printf '%s\\n' \"$1\" \"$2\"", "sh",
              "有 空白 的 參數", "中文與 \"引號\""};
    exec.run(c);
    check::eq(slurp(c.stdout_path),
              std::string{"有 空白 的 參數\n中文與 \"引號\"\n"},
              "★ 參數邊界原樣傳給子行程，沒有被重新拼接或再解析");
}

void test_clean_environment() {
    check::section("★★ 預設環境是乾淨的，不是繼承來的");
    ExecExecutor exec;

    // 在我們自己的環境裡放一個很好認的東西。
    ::setenv("AOS_SIMPLE_LEAK_CHECK", "leaked", 1);

    Call c = workspace();
    c.argv = {"/usr/bin/env"};
    exec.run(c);
    const std::string seen = slurp(c.stdout_path);

    check::ok(seen.find("AOS_SIMPLE_LEAK_CHECK") == std::string::npos,
              "★ 我們自己 export 的東西不會漏給子行程");
    check::ok(seen.find("PATH=") != std::string::npos, "PATH 有給");
    // 只有 PATH 一項，所以就是一行。
    check::eq(seen, std::string{"PATH=/usr/local/bin:/usr/bin:/bin\n"},
              "★ 乾淨環境裡就只有 PATH");

    check::eq(default_environment().size(), std::size_t{1},
              "default_environment() 也是同一份");

    ::unsetenv("AOS_SIMPLE_LEAK_CHECK");
}

void test_env_file() {
    check::section("★ env 檔：exec 前先 source");
    ExecExecutor exec;

    // 基本：export 一個變數
    {
        Call c = workspace();
        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "export GREETING='你好'\nexport COUNT=3\n");
        c.env = envfile;
        c.argv = {"/bin/sh", "-c", "printf '%s/%s\\n' \"$GREETING\" \"$COUNT\""};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::Exited, "跑起來了");
        check::eq(slurp(c.stdout_path), std::string{"你好/3\n"},
                  "★ env 檔 export 的東西子行程看得到");
    }

    // ★ source 的基底是那份乾淨環境，所以 $PATH 展開得出來、而且是可預測的
    {
        Call c = workspace();
        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "export PATH=\"$PATH:/opt/aos\"\n");
        c.env = envfile;
        c.argv = {"/bin/sh", "-c", "printf '%s\\n' \"$PATH\""};
        exec.run(c);
        check::eq(slurp(c.stdout_path),
                  std::string{"/usr/local/bin:/usr/bin:/bin:/opt/aos\n"},
                  "★ 基底是乾淨環境，所以 $PATH 疊加的結果是可預測的");
    }

    // env 檔可以跑任意 shell（這是 source 而不是 KEY=VALUE 的意義）
    {
        Call c = workspace();
        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "if [ -d /tmp ]; then export HAS_TMP=yes; else export HAS_TMP=no; fi\n"
                      "export COMPUTED=\"$(printf 'a%sc' b)\"\n");
        c.env = envfile;
        c.argv = {"/bin/sh", "-c", "printf '%s %s\\n' \"$HAS_TMP\" \"$COMPUTED\""};
        exec.run(c);
        check::eq(slurp(c.stdout_path), std::string{"yes abc\n"},
                  "★ 條件式與命令替換都能用——這才叫 source");
    }

    // ★ env 檔自己印的東西要進 stderr，不可以汙染環境傾印
    {
        Call c = workspace();
        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "echo '載入中…'\nexport OK=1\n");
        c.env = envfile;
        c.argv = {"/bin/sh", "-c", "printf '%s\\n' \"$OK\""};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::Exited, "還是跑得起來");
        check::eq(slurp(c.stdout_path), std::string{"1\n"},
                  "★ env 檔的 echo 沒有混進環境裡");
        check::ok(slurp(c.stderr_path).find("載入中") != std::string::npos,
                  "★ 而是進了這次呼叫的 stderr 證據檔");
    }

    // env 檔不存在 -> 起不來
    {
        Call c = workspace();
        c.env = "/no/such/profile";
        c.argv = {"/bin/echo", "never"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::LaunchError, "★ env 檔不存在 => LaunchError");
        check::eq(slurp(c.stdout_path), std::string{""}, "目標程式一個字都沒跑");
        check::eq(slurp(c.exit_path), std::string{"126\n"}, "exit 檔是 126");
        check::ok(!slurp(c.stderr_path).empty(), "★ sh 的抱怨留在 stderr 證據檔裡");
    }

    // env 檔裡有錯 -> 起不來
    {
        Call c = workspace();
        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "exit 3\n");
        c.env = envfile;
        c.argv = {"/bin/echo", "never"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::LaunchError, "env 檔 exit 非 0 => LaunchError");
        check::eq(slurp(c.stdout_path), std::string{""}, "目標程式沒跑");
    }

    // ★★ env 檔提早 exit 0：哨兵擋下來的那個案例
    {
        Call c = workspace();
        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "export SOMETHING=1\nexit 0\n");
        c.env = envfile;
        c.argv = {"/bin/echo", "never"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::LaunchError,
                  "★★ 提早 exit 0 => LaunchError，不是拿一份空環境去跑");
        check::ok(o.reason.find("不完整") != std::string::npos,
                  "而且說得出是環境傾印不完整");
        check::eq(slurp(c.stdout_path), std::string{""}, "目標程式沒跑");
    }

    // env 檔不能偷吃這次呼叫的 stdin
    {
        Call c = workspace("給目標程式的資料\n");
        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "cat > /dev/null\nexport OK=1\n");
        c.env = envfile;
        c.argv = {"/bin/cat"};
        exec.run(c);
        check::eq(slurp(c.stdout_path), std::string{"給目標程式的資料\n"},
                  "★ env 檔的 stdin 是 /dev/null，吃不到目標程式的那份");
    }
}

void test_path_lookup() {
    check::section("★ PATH 查找");
    ExecExecutor exec;

    // 裸名字：用最終環境的 PATH 找
    {
        Call c = workspace();
        c.argv = {"sh", "-c", "printf ok"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::Exited, "★ 裸名字 sh 找得到");
        check::eq(slurp(c.stdout_path), std::string{"ok"}, "而且真的跑了");
    }

    // 找不到 -> ENOENT -> 127
    {
        Call c = workspace();
        c.argv = {"no-such-command-xyz"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::LaunchError, "找不到是 LaunchError");
        check::eq(o.err, ENOENT, "errno 是 ENOENT");
        check::eq(slurp(c.exit_path), std::string{"127\n"}, "exit 檔是 127");
    }

    // ★ 用的是「這次呼叫的 PATH」，不是我們自己的 PATH
    {
        Call c = workspace();
        const std::string bin = dir_of(c) + "/mybin";
        ::mkdir(bin.c_str(), 0700);
        spit(bin + "/only-here", "#!/bin/sh\nprintf found-it\n");
        ::chmod((bin + "/only-here").c_str(), 0700);

        // 先確認沒有 env 檔的時候找不到（乾淨 PATH 裡沒有這個目錄）
        Call without = workspace();
        without.argv = {"only-here"};
        check::ok(exec.run(without).status == Status::LaunchError,
                  "乾淨 PATH 裡找不到它");

        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "export PATH=\"" + bin + ":$PATH\"\n");
        c.env = envfile;
        c.argv = {"only-here"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::Exited,
                  "★ env 檔把目錄加進 PATH 之後就找得到了");
        check::eq(slurp(c.stdout_path), std::string{"found-it"}, "而且跑的是那一支");
    }

    // 找得到但不能執行 -> EACCES -> 126
    {
        Call c = workspace();
        const std::string bin = dir_of(c) + "/mybin";
        ::mkdir(bin.c_str(), 0700);
        spit(bin + "/not-runnable", "data\n");
        ::chmod((bin + "/not-runnable").c_str(), 0600);

        const std::string envfile = dir_of(c) + "/profile";
        spit(envfile, "export PATH=\"" + bin + ":$PATH\"\n");
        c.env = envfile;
        c.argv = {"not-runnable"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::LaunchError, "沒執行權限是 LaunchError");
        check::eq(o.err, EACCES, "★ errno 收斂成 EACCES，不是 ENOENT");
        check::eq(slurp(c.exit_path), std::string{"126\n"},
                  "★ 所以是 126，跟找不到的 127 分得開");
    }

    // 含 '/' 就不查找
    {
        Call c = workspace();
        c.argv = {"./nope"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::LaunchError, "含 / 就當路徑用，找不到就是找不到");
        check::eq(o.err, ENOENT, "不會跑去 PATH 裡亂找");
    }
}

void test_timeout() {
    check::section("★★ 逾時");

    ExecExecutor exec{[](const Call &) {
        ExecOptions o;
        o.timeout = std::chrono::milliseconds{200};
        o.grace = std::chrono::milliseconds{100};
        return o;
    }};

    // 跑不完的要被砍
    {
        Call c = workspace();
        c.argv = {"/bin/sh", "-c", "sleep 30"};
        const auto t0 = std::chrono::steady_clock::now();
        const Outcome o = exec.run(c);
        const double took =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        check::ok(o.timed_out, "★★ 標成逾時");
        check::ok(took < 3.0, "★ 真的被砍了，沒有等 30 秒（" + std::to_string(took) + "）");
        check::eq(slurp(c.exit_path), std::string{"124\n"},
                  "★ exit 檔是 124（timeout(1) 的慣例）");
    }

    // ★★ 逾時跟「外面有人砍它」要分得開——沒有旗標的話兩者都是 Signalled{9}
    {
        Call c = workspace();
        c.argv = {"/bin/sh", "-c", "kill -9 $$"};
        const Outcome o = exec.run(c);
        check::ok(o.status == Status::Signalled, "被信號殺死");
        check::ok(!o.timed_out, "★★ 但**不是**逾時——旗標分得開");
        check::eq(slurp(c.exit_path), std::string{"137\n"}, "exit 檔是 137，不是 124");
    }

    // 來得及跑完的不受影響
    {
        Call c = workspace();
        c.argv = {"/bin/echo", "快"};
        const Outcome o = exec.run(c);
        check::ok(!o.timed_out, "★ 沒超時就不標");
        check::eq(o.code, 0, "正常結束");
        check::eq(slurp(c.stdout_path), std::string{"快\n"}, "輸出正常");
    }

    // 不給 policy = 不限，行為跟以前一樣
    {
        ExecExecutor plain;
        Call c = workspace();
        c.argv = {"/bin/sh", "-c", "sleep 0.3; printf ok"};
        const Outcome o = plain.run(c);
        check::ok(!o.timed_out, "★ 沒給 policy 就是不限，不會被砍");
        check::eq(slurp(c.stdout_path), std::string{"ok"}, "跑完了");
    }

    // ★ SIGTERM 收得到的話讓它自己收尾
    {
        ExecExecutor gentle{[](const Call &) {
            ExecOptions o;
            o.timeout = std::chrono::milliseconds{150};
            o.grace = std::chrono::milliseconds{2000};
            return o;
        }};
        Call c = workspace();
        c.argv = {"/bin/sh", "-c",
                  "trap 'printf 收尾了; exit 0' TERM; while :; do sleep 0.05; done"};
        const Outcome o = gentle.run(c);
        check::ok(o.timed_out, "還是標成逾時");
        check::eq(slurp(c.stdout_path), std::string{"收尾了"},
                  "★★ 先 SIGTERM 讓它有機會自己收尾，不是直接 KILL");
        check::eq(slurp(c.exit_path), std::string{"124\n"},
                  "★ 就算它自己乾淨 exit(0)，還是記成逾時");
    }

    // ★★ 孫行程也要被砍掉，不能留下孤兒
    {
        ExecExecutor hard{[](const Call &) {
            ExecOptions o;
            o.timeout = std::chrono::milliseconds{200};
            o.grace = std::chrono::milliseconds{100};
            return o;
        }};
        Call c = workspace();
        const std::string marker = dir_of(c) + "/grandchild-alive";
        // 孫行程：活著就一直摸那個檔
        c.argv = {"/bin/sh", "-c",
                  "( while :; do : > '" + marker + "'; sleep 0.05; done ) & sleep 30"};
        hard.run(c);

        ::unlink(marker.c_str());
        ::usleep(400 * 1000);   // 給孤兒足夠時間再摸一次
        check::ok(!exists(marker),
                  "★★ 孫行程也被砍了（砍的是行程群組，不是單一 pid）");
    }
}

void test_output_cap() {
    check::section("★ 輸出上限");

    ExecExecutor exec{[](const Call &) {
        ExecOptions o;
        o.max_output_bytes = 64 * 1024;
        o.grace = std::chrono::milliseconds{100};
        return o;
    }};

    {
        Call c = workspace();
        c.argv = {"/bin/sh", "-c", "while :; do printf 'aaaaaaaaaaaaaaaaaaaaaaaa'; done"};
        const Outcome o = exec.run(c);
        check::ok(o.output_capped, "★ 標成輸出爆量");
        check::ok(!o.timed_out, "★ 而且不是逾時——兩個原因分得開");
        check::eq(slurp(c.exit_path), std::string{"123\n"}, "exit 檔是 123");
    }

    {
        Call c = workspace();
        c.argv = {"/bin/echo", "一點點"};
        const Outcome o = exec.run(c);
        check::ok(!o.output_capped, "★ 沒超過就不標");
        check::eq(o.code, 0, "正常結束");
    }
}

// ★ 回歸測試：parent 沒沖出去的 stdio buffer 不可以漏進證據檔。
//
// ⚠ 這一項在**一般建置下抓不到**，會假性通過：子行程走的是 _exit，而 _exit
//   本來就不沖 stdio。真正會現形的是 `make sanitize`——ASan／TSan 攔截 _exit，
//   它們的 teardown 會沖。實測拿掉 fflush 之後就是那一輪紅的。
//   （另一個前提是 stdout 得是全緩衝：管線／檔案會，終端機不會。）
//
//   所以這一項的價值不是「每次都會抓到」，是「改壞的時候 sanitize 那輪會紅」。
//   動過 exec 這段就順手跑一次 make sanitize。
void test_no_buffer_leak() {
    check::section("★ parent 的 stdio buffer 不會漏進證據檔");
    ExecExecutor exec;

    Call c = workspace();
    // 這支起不來，所以子行程從頭到尾一個位元組都不會往 stdout 寫。
    // 檔案裡出現任何東西，就一定是從 parent 的 buffer 漏過去的。
    c.argv = {"/no/such/binary-xyz"};

    std::printf("這一行故意不換行也不 flush，它不可以出現在證據檔裡");
    const Outcome o = exec.run(c);
    std::fflush(stdout);  // 讓後面的檢查訊息正常顯示

    check::ok(o.status == Status::LaunchError, "起不來");
    check::eq(slurp(c.stdout_path), std::string{""},
              "★ stdout 檔是空的——fork 前有 fflush(nullptr)");
    check::eq(slurp(c.stderr_path), std::string{""}, "★ stderr 檔也是空的");
}

void test_through_the_loop() {
    check::section("接上 loop");

    Queue q;
    ExecExecutor exec;
    std::vector<Outcome> results;
    Loop loop{q, exec, [&](const Call &, const Outcome &o) { results.push_back(o); }};

    Call ok = workspace();
    ok.argv = {"/bin/sh", "-c", "exit 3"};
    Call bad = workspace();
    bad.argv = {""};  // argv[0] 空字串，形狀就不對
    Call after = workspace();
    after.argv = {"/bin/echo", "still here"};

    q.push(ok);
    q.push(bad);
    q.push(after);
    q.close();
    loop.run();

    check::eq(loop.stats().handled, 3ull, "三個都處理了");
    check::eq(loop.stats().rejected, 1ull, "其中一個被拒絕");
    check::ok(results[0].status == Status::Exited, "第一個跑了");
    check::ok(results[1].status == Status::Rejected, "第二個被拒絕");
    check::ok(results[2].status == Status::Exited, "★ 前面出過事，後面照跑");
    check::eq(slurp(after.stdout_path), std::string{"still here\n"}, "而且真的跑了");
}

// ★★ exec 這條路：argv[0] == "exec" -> 專屬 loop，同時最多四個。
void test_exec_route() {
    check::section("★★ exec 路由：同時最多四個");

    Queue exec_queue;
    ExecExecutor raw;
    DropArgv0Executor gated{raw};  // 把 "exec" 這個路由鍵吃掉

    std::vector<Outcome> results;
    std::vector<Call> done;
    Pool pool{exec_queue, gated, kDefaultExecWorkers,
              [&](const Call &call, const Outcome &outcome) {
                  done.push_back(call);
                  results.push_back(outcome);
              }};

    Router router;
    router.route("exec", exec_queue);
    // ★ 刻意**不設 fallback**：最初入口預設關閉，想 exec 就得走 exec 這條。

    check::eq(pool.workers(), kDefaultExecWorkers, "起了四條 worker");

    // 沒走 exec 的一律沒地方去
    Call direct = workspace();
    direct.argv = {"/bin/echo", "想抄捷徑"};
    check::ok(router.dispatch(direct).result == Router::Result::NoRoute,
              "★ 不走 exec 的直接呼叫沒有入口（NoRoute）");
    check::ok(!exists(direct.stdout_path), "而且真的什麼都沒發生");

    // 走 exec 就通
    constexpr int kJobs = 12;
    std::vector<Call> calls;
    for (int i = 0; i < kJobs; ++i) {
        Call c = workspace();
        c.argv = {"exec", "/bin/sh", "-c", "sleep 0.15; printf done"};
        calls.push_back(c);
    }

    const auto started = std::chrono::steady_clock::now();
    for (const Call &c : calls) {
        check::ok(router.dispatch(c).result == Router::Result::Routed, "");
        check::checks--;  // 上面那行只是防呆，不要灌爆檢查數
    }
    exec_queue.close();
    pool.join();
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    check::eq(pool.stats().handled, static_cast<unsigned long long>(kJobs),
              "12 個都做完了");

    bool all_ok = true;
    for (const Outcome &o : results) {
        if (o.status != Status::Exited || o.code != 0) all_ok = false;
    }
    check::ok(all_ok, "12 個都成功");

    bool all_wrote = true;
    for (const Call &c : calls) {
        if (slurp(c.stdout_path) != "done") all_wrote = false;
    }
    check::ok(all_wrote, "★ 路由鍵 exec 被吃掉了，真正跑的是後面那個命令");

    // ★ 上限：同時最多四個
    check::ok(pool.peak_in_flight() <= kDefaultExecWorkers,
              "★★ 同時執行數從沒超過 4");
    check::eq(pool.peak_in_flight(), kDefaultExecWorkers,
              "★★ 而且真的用滿了 4（不是退化成一次一個）");

    // 12 個 × 0.15 秒序列化要 1.8 秒；四條併發約 0.45 秒。
    // 抓 1.2 秒當門檻，慢機器也不會誤判，但序列化一定過不了。
    check::ok(elapsed < 1.2,
              "★ 總時間符合四條併發（" + std::to_string(elapsed) + " 秒，序列化要 1.8）");

    // exec 後面什麼都沒帶 -> 吃掉路由鍵之後 argv 空了 -> 形狀不對
    Queue q2;
    ExecExecutor raw2;
    DropArgv0Executor gated2{raw2};
    std::vector<Outcome> r2;
    Pool pool2{q2, gated2, 2, [&](const Call &, const Outcome &o) { r2.push_back(o); }};
    Call bare = workspace();
    bare.argv = {"exec"};
    q2.push(bare);
    q2.close();
    pool2.join();
    check::ok(r2.size() == 1 && r2[0].status == Status::Rejected,
              "★ 只送了 exec 沒帶命令 => Rejected");
    check::eq(slurp(bare.exit_path), std::string{"125\n"}, "而且留下 125");
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/aos-simple-exec-XXXXXX";
    const char *made = ::mkdtemp(tmpl);
    if (made == nullptr) {
        std::fprintf(stderr, "開不了暫存目錄\n");
        return 1;
    }
    root = made;

    test_success();
    test_stdin_and_cwd();
    test_nonzero();
    test_signal_vs_exit();
    test_launch_error();
    test_rejected();
    test_overwrite();
    test_unknown_marker();
    test_exit_number_mapping();
    test_clean_environment();
    test_env_file();
    test_path_lookup();
    test_argv_fidelity();
    test_timeout();
    test_output_cap();
    test_no_buffer_leak();
    test_through_the_loop();
    test_exec_route();

    const int result = check::report();
    // 收尾。跑失敗的話留著現場給人看。
    if (result == 0) {
        const std::string cmd = "rm -rf '" + root + "'";
        if (std::system(cmd.c_str()) != 0) {
            std::fprintf(stderr, "清不掉 %s\n", root.c_str());
        }
    } else {
        std::fprintf(stderr, "現場留在 %s\n", root.c_str());
    }
    return result;
}
