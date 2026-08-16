// plugin.cpp — aos-core 的外掛表面。這個檔是唯一認識 aos ABI 的地方。
//
//     aos lisp eval <loop> <form…>   在某條 loop 上求值
//     aos lisp loops                 列出目前有哪些 loop
//     aos lisp stop <loop>           叫一條 loop 收工
//
// ── 為什麼是 .so 外掛，不是編進去的模組 ─────────────────────────────
//
// aos-core 的 README 說得很清楚：「LLM 呼叫、工具設定檔那一層不在這裡，
// 它會是一個獨立專案，以外掛的形式接上來。」走 .so 的話 aos-core **一行都不用改**
// （編進去的話要動它的 modules.c 和 Makefile），aos-lisp 保持自足。
// 我們用到的只有 AOS_PLUGIN_ABI_VERSION 1 這個穩定介面。
//
// ── 執行緒 ──────────────────────────────────────────────────────────
//
// plugin.h 說：「同一個 run() 可能同時被多條執行緒呼叫。」所以
//   * 登記處有鎖（loop.cpp）；
//   * ★ 連線執行緒**絕對不碰任何 janet_***——VM 狀態是 thread-local，
//     只有 loop 自己那條執行緒可以碰。這裡跟 loop 之間只交換位元組（job.hpp）。

#include <string>
#include <vector>

#include "aos/plugin.hpp"
#include "loop.hpp"

namespace {

const aos_host *host_ = nullptr;

// 把 argv 併成一段原始碼。
//
// 一個參數就原樣用；多個就用空白接起來，因為 shell 已經照空白拆過了
// （`aos lisp eval a '(+ 1 2)'` 是一個，`aos lisp eval a "(+" "1" "2)"` 是三個）。
// ⚠ 這是有損的：參數裡本來就有的空白會跟分隔用的空白混在一起。要精確就用單引號
//   包成一個參數，或走 stdin。
std::string join_source(int argc, const char *const *argv, int from) {
    std::string source;
    for (int i = from; i < argc; ++i) {
        if (!source.empty()) source += ' ';
        source += argv[i];
    }
    return source;
}

std::string read_all_stdin(aos_session *session) {
    std::string all;
    std::string chunk;
    while (aos::read_chunk(session, chunk)) all += chunk;
    return all;
}

int cmd_eval(aos_session *session, int argc, const char *const *argv, void *) {
    if (argc < 1) {
        aos::complain(session,
                      "用法：aos lisp eval <loop> <form…>\n"
                      "      沒給 form 就從 stdin 讀。\n");
        return 2;
    }
    const std::string loop_name = argv[0];
    if (loop_name.empty()) {
        aos::complain(session, "loop 名字不能是空的\n");
        return 2;
    }

    auto job = std::make_shared<aoslisp::Job>();
    job->source = argc > 1 ? join_source(argc, argv, 1) : read_all_stdin(session);
    if (job->source.empty()) {
        aos::complain(session, "沒有東西可以求值\n");
        return 2;
    }

    // 沒有這條 loop 就開一條。★ 開一條 = 開一條執行緒 + 一個 Janet VM
    //   + 一張全新的長存 env。
    aoslisp::Loop &loop = aoslisp::registry().acquire(loop_name);
    if (!loop.submit(job)) {
        aos::complain(session, "loop『" + loop_name + "』正在收工，沒收下這個請求\n");
        return 1;
    }

    job->wait();  // 阻塞。daemon 是一條連線一條執行緒，所以擋在這裡沒關係。

    if (!job->out.empty()) host_->write_output(session, job->out.data(), job->out.size());
    if (!job->err.empty()) host_->write_error(session, job->err.data(), job->err.size());
    return job->status;
}

int cmd_loops(aos_session *session, int, const char *const *, void *) {
    const std::vector<aoslisp::LoopInfo> loops = aoslisp::registry().snapshot();
    if (loops.empty()) {
        aos::say(session, "還沒有任何 loop。`aos lisp eval <名字> …` 會開一條。\n");
        return 0;
    }
    std::string out = "名字                 做過      排隊中\n";
    for (const aoslisp::LoopInfo &info : loops) {
        std::string name = info.name;
        if (name.size() < 20) name.resize(20, ' ');
        out += name + "  " + std::to_string(info.handled) + "\t" +
               std::to_string(info.depth) + "\n";
    }
    return aos::say(session, out) == AOS_OK ? 0 : 1;
}

int cmd_stop(aos_session *session, int argc, const char *const *argv, void *) {
    if (argc < 1) {
        aos::complain(session, "用法：aos lisp stop <loop>\n");
        return 2;
    }
    // 排在前面的 job 會做完才停——跟 aos daemon stop 同一個語義。
    if (!aoslisp::registry().stop(argv[0])) {
        aos::complain(session, std::string{"沒有叫『"} + argv[0] + "』的 loop\n");
        return 1;
    }
    return aos::say(session, std::string{"loop『"} + argv[0] + "』收工了\n") == AOS_OK ? 0 : 1;
}

// aos::guarded<> 把例外攔在 C 邊界之前。例外穿過 C 邊界是 UB，實務上就是整個
// daemon 消失，而且爆炸的不會是我們寫的那行。
const aos_command lisp_children[] = {
    {"eval", "在某條 loop 上求值一段 Janet", aos::guarded<cmd_eval>, nullptr, nullptr, 0},
    {"loops", "列出目前有哪些 loop", aos::guarded<cmd_loops>, nullptr, nullptr, 0},
    {"stop", "叫一條 loop 收工", aos::guarded<cmd_stop>, nullptr, nullptr, 0},
};

const aos_command commands[] = {
    {"lisp", "Janet loop：一條 loop 一個 VM 一張長存 env", nullptr, nullptr,
     lisp_children, sizeof lisp_children / sizeof lisp_children[0]},
};

void plugin_shutdown(void) {
    // daemon 在所有連線都結束之後、dlclose 之前呼叫這裡。
    // ★ 一定要在這裡把 loop 執行緒 join 掉：它們不是 aos-core 的連線，
    //   aos_runtime_active_connections 數不到它們，所以沒有別人會等它們。
    //   dlclose 之後這些執行緒的程式碼就從記憶體消失了。
    // ⚠ 已知限制：某條 loop 手上如果有一個永遠不返回的任務，daemon 的收工
    //   就會卡在這裡。loop 在**兩個 job 之間**才看得到哨兵。
    aoslisp::registry().shutdown_all();
}

const aos_plugin plugin = {
    AOS_PLUGIN_ABI_VERSION,
    "aos-lisp",
    commands,
    sizeof commands / sizeof commands[0],
    plugin_shutdown,
};

}  // namespace

// ★ 一定要 extern "C"，否則名字會被 mangle，dlsym 找不到。
extern "C" const aos_plugin *aos_plugin_init(const aos_host *host) {
    host_ = host;
    return &plugin;
}
