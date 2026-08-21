#ifndef AOS_PLUGIN_HPP
#define AOS_PLUGIN_HPP
/* aos/plugin.hpp — 用 C++ 寫 aos 模組／外掛時多包一層。
 *
 * C 的部分全部在 aos/plugin.h，這個檔只解決一件事：
 *
 *   ## 例外絕對不能穿過 C 邊界
 *
 * aos 的 run() 是從 C 呼叫過來的。讓例外從那裡飛出去是**未定義行為**——
 * 實務上通常是直接 std::terminate 把整個 daemon 帶走。而且它不會在你寫的
 * 那一行爆炸，是在別人的 C 迴圈裡爆炸，所以第一眼完全看不出關聯。
 *
 * 這件事沒辦法靠自律，因為會丟例外的多半不是你寫的那行——是 std::stoi、
 * 是 vector::at、是 new。所以這裡提供一個包裝，讓你**想忘也忘不掉**：
 *
 *     #include "aos/plugin.hpp"
 *
 *     static int greet(aos_session *s, int argc, const char *const *argv, void *) {
 *         if (argc == 0) throw std::runtime_error("要給一個名字");  // 安全
 *         ...
 *         return 0;
 *     }
 *
 *     static const aos_command commands[] = {
 *         {"greet", "打招呼", aos::guarded<greet>, nullptr, nullptr, 0},
 *         //                  ^^^^^^^^^^^^^^^^^^ 就這樣
 *     };
 *
 * 例外會被接住、訊息寫到呼叫端的 stderr、回傳 exit code 1。
 *
 *   ## 匯出進入點時要 extern "C"
 *
 *     extern "C" const aos_plugin *aos_module_greet_init(const aos_host *host) {
 *         host_ = host;
 *         return &plugin;
 *     }
 *
 * 沒有 extern "C" 的話名字會被 mangle，modules.c 或 dlsym 都找不到它。 */

#include "aos/plugin.h"

#include <cstring>
#include <exception>
#include <string>

namespace aos {

using command_fn = int (*)(aos_session *, int, const char *const *, void *);

namespace detail {

inline void report(aos_session *session, const std::string &message) noexcept {
    /* 這裡自己也不能丟。寫失敗就算了——連線大概已經斷了。 */
    const aos_host *host = aos_host_of(session);
    if (host != nullptr && host->write_error != nullptr) {
        host->write_error(session, message.data(), message.size());
    }
}

}  // namespace detail

/* 把一個可能丟例外的 run 包成 C 能安全呼叫的樣子。 */
template <command_fn Fn>
int guarded(aos_session *session, int argc, const char *const *argv,
            void *user) noexcept {
    try {
        return Fn(session, argc, argv, user);
    } catch (const std::exception &error) {
        try {
            detail::report(session, std::string{error.what()} + "\n");
        } catch (...) {
            /* 連組錯誤訊息都失敗（多半是記憶體不足）。放棄，但別讓它飛出去。 */
        }
        return 1;
    } catch (...) {
        detail::report(session, "模組丟出了一個不是 std::exception 的東西\n");
        return 1;
    }
}

/* 寫一段 std::string 出去的便利函式，省得每次都算長度。 */
inline int say(aos_session *session, const std::string &text) {
    return aos_host_of(session)->write_output(session, text.data(), text.size());
}

inline int complain(aos_session *session, const std::string &text) {
    return aos_host_of(session)->write_error(session, text.data(), text.size());
}

/* 讀一塊 stdin。回 false 代表結束了（EOF 或連線壞掉）。 */
inline bool read_chunk(aos_session *session, std::string &out,
                       std::size_t cap = 64 * 1024) {
    out.resize(cap);
    std::size_t got = 0;
    const int state =
        aos_host_of(session)->read_input(session, &out[0], cap, &got);
    out.resize(state == AOS_OK ? got : 0);
    return state == AOS_OK;
}

}  // namespace aos

#endif /* AOS_PLUGIN_HPP */
