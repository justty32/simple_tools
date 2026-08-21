/* greet.cpp — 用 C++ 寫、**編進 aos-core** 的模組範例。
 *
 *   make MODULE_SRCS=examples/module-cpp/greet.cpp
 *   ./bin/aos-daemon &
 *   ./bin/aos greet 世界              → 你好，世界
 *   ./bin/aos greet count 3           → 1 2 3
 *   ./bin/aos greet boom              → 示範例外被接住，不會帶走 daemon
 *
 * 跟 .so 外掛的差別只有載入方式，契約是同一份。
 * 三件事值得看：
 *   1. run 全部包在 aos::guarded<> 裡——例外不會穿過 C 邊界
 *   2. 進入點是 extern "C"，不然 modules.c 連不到
 *   3. 用得起 std::string、容器、例外，就是一般的 C++ */

#include "aos/plugin.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

const aos_host *host;

int greet(aos_session *session, int argc, const char *const *argv, void *) {
    if (argc == 0) {
        /* 直接丟。guarded 會接住、寫進呼叫端的 stderr、回 exit code 1。 */
        throw std::invalid_argument("用法：aos greet <名字>");
    }
    std::string line = "你好，";
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            line += "、";
        }
        line += argv[i];
    }
    line += "\n";
    return aos::say(session, line);
}

int count(aos_session *session, int argc, const char *const *argv, void *) {
    if (argc != 1) {
        throw std::invalid_argument("用法：aos greet count <數字>");
    }
    /* std::stoi 會丟——正是「不是你寫的那行在丟」的例子。
     * 沒有 guarded 的話，使用者打 `aos greet count abc` 就能弄掉整個 daemon。 */
    const int upto = std::stoi(argv[0]);

    std::string out;
    for (int i = 1; i <= upto; ++i) {
        out += std::to_string(i);
        out += i == upto ? '\n' : ' ';
    }
    return aos::say(session, out);
}

/* 把 stdin 逐塊反轉。示範 C++ 這邊照樣是串流的。 */
int reverse(aos_session *session, int, const char *const *, void *) {
    std::string chunk;
    while (aos::read_chunk(session, chunk)) {
        std::string flipped(chunk.rbegin(), chunk.rend());
        host->write_output(session, flipped.data(), flipped.size());
    }
    return 0;
}

int boom(aos_session *, int, const char *const *, void *) {
    throw std::runtime_error("這是一個故意丟出來的例外，daemon 應該還活著");
}

const aos_command greet_children[] = {
    {"count", "數到 N（示範 stoi 丟例外）", aos::guarded<count>, nullptr, nullptr,
     0},
    {"reverse", "把 stdin 逐塊反轉（串流）", aos::guarded<reverse>, nullptr,
     nullptr, 0},
    {"boom", "故意丟例外，證明 daemon 不會倒", aos::guarded<boom>, nullptr,
     nullptr, 0},
};

/* greet 自己也能跑（有 run），同時又有子命令。兩者可以並存。 */
const aos_command greet_commands[] = {
    {"greet", "C++ 模組範例", aos::guarded<greet>, nullptr, greet_children,
     sizeof greet_children / sizeof greet_children[0]},
};

const aos_plugin greet_plugin = {
    AOS_PLUGIN_ABI_VERSION,
    "greet(C++)",
    greet_commands,
    sizeof greet_commands / sizeof greet_commands[0],
    nullptr,
};

}  // namespace

/* extern "C" 不能省：沒有它名字會被 mangle，modules.c 連不到。 */
extern "C" const aos_plugin *aos_module_greet_init(const aos_host *given) {
    if (given == nullptr || given->abi_version != AOS_PLUGIN_ABI_VERSION) {
        return nullptr;
    }
    host = given;
    return &greet_plugin;
}
