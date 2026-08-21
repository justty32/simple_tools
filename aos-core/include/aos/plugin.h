#ifndef AOS_PLUGIN_H
#define AOS_PLUGIN_H
/* aos/plugin.h — 外掛唯一要 include 的標頭，也是 aos 對外的全部承諾。
 *
 * 一個外掛就是一個 .so，匯出一個符號 aos_plugin_init，回一張命令表。
 * daemon 啟動時 dlopen 它，把那張表接進命令樹——之後 `aos 你的命令` 就會走到你。
 *
 * ## 一個命令看得到的世界只有三件套
 *
 *     stdin / stdout / stderr   aos_host 的 read_input / write_output / write_error
 *     argv                      run() 的 argc、argv（已經去掉命令路徑本身）
 *     exit status               run() 的回傳值
 *
 * 就這樣。跟一個 Linux 程式看到的一樣，沒有第四樣東西要學。
 *
 * ## 全部都是阻塞的，這是刻意的
 *
 * daemon 是一條連線一條執行緒，所以你的 run() 裡可以直接寫**平鋪直敘的程式**：
 * 讀一塊、算一算、寫回去，想擋多久就擋多久，不會影響別的連線。
 * 要打 HTTP 就直接 curl_easy_perform()，不必為了不卡住事件迴圈去拆狀態機。
 *
 * ## 二進位安全
 *
 * 所有資料都是 (指標, 長度)，**沒有一處靠 NUL 結尾**。payload 裡可以有 \0。
 * 唯一的例外是那些明講「NUL 結尾」的字串欄位（name、summary、argv）。
 *
 * ## 記憶體約定：誰配置誰負責，而且沒有人需要釋放對方的東西
 *
 * - host 不會給你任何要你 free 的東西。回傳的 const char* 只在該次呼叫期間有效。
 * - 你回給 host 的 aos_plugin / aos_command 表，**必須活到 shutdown 為止**
 *   （最簡單也最建議的做法：全部宣告成 static）。host 不會複製、也不會釋放它。
 *
 * ## 執行緒
 *
 * 同一個 run() **可能同時被多條執行緒呼叫**（多個 CLI 同時打同一個命令）。
 * aos_session 是每次呼叫各一份，不必鎖；但你自己的全域狀態要自己鎖。
 *
 * ## 最小範例
 *
 *     static int shout(aos_session *s, int argc, const char *const *argv, void *u) {
 *         (void)u;
 *         for (int i = 0; i < argc; ++i) {
 *             aos_host_of(s)->write_output(s, argv[i], strlen(argv[i]));
 *         }
 *         return 0;
 *     }
 *     static const aos_command commands[] = {
 *         {"shout", "把參數喊回去", shout, NULL, NULL, 0},
 *     };
 *     static const aos_plugin plugin = {
 *         AOS_PLUGIN_ABI_VERSION, "hello", commands, 1, NULL,
 *     };
 *     const aos_plugin *aos_plugin_init(const aos_host *host) {
 *         (void)host;
 *         return &plugin;
 *     }
 *
 * 完整可編譯的版本見 examples/plugin-hello/。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 這個數字只有在**既有欄位的意義改變**時才加一。純粹在結構尾端追加欄位不加，
 * 因為舊外掛用 sizeof 算出來的那份仍然對得起來。 */
#define AOS_PLUGIN_ABI_VERSION 1u

/* 一次命令執行。不透明，別存起來——它在 run() 回傳後就沒了。 */
typedef struct aos_session aos_session;

/* 回傳碼。 */
#define AOS_OK 0
#define AOS_EOF 1     /* read_input：對方的 stdin 結束了 */
#define AOS_BROKEN (-1) /* 連線壞了。別再寫，直接讓 run() 返回 */

/* host 提供給外掛的全部能力。 */
typedef struct aos_host {
    uint32_t abi_version;

    /* 阻塞讀一塊 stdin。讀到東西回 AOS_OK 並設好 *out_len；
     * stdin 結束回 AOS_EOF（*out_len 為 0）。buf 不會被補 NUL。 */
    int (*read_input)(aos_session *session, void *buf, size_t cap,
                      size_t *out_len);

    /* 全部寫完才返回。data 可以含 \0。 */
    int (*write_output)(aos_session *session, const void *data, size_t len);
    int (*write_error)(aos_session *session, const void *data, size_t len);

    /* 呼叫端的工作目錄，NUL 結尾。只在這次呼叫期間有效。 */
    const char *(*working_directory)(aos_session *session);

    /* daemon 從啟動到現在幾秒。 */
    uint64_t (*uptime_seconds)(aos_session *session);

    /* 寫到 **daemon 自己的 stderr**（不是呼叫端的）。給診斷用，
     * 使用者看得到的訊息請走 write_error。 */
    void (*log)(const char *message);
} aos_host;

/* 一個命令。 */
typedef struct aos_command {
    const char *name;    /* NUL 結尾 */
    const char *summary; /* NUL 結尾，help 會列出來 */

    /* argv **不含**命令路徑本身：`aos foo bar --x` 走到 bar 時 argv 是 {"--x"}。
     * 回傳值就是 exit status。NULL 代表這是純分組節點，只會列出 children。 */
    int (*run)(aos_session *session, int argc, const char *const *argv,
               void *user);

    void *user; /* 原樣傳回給 run，host 不碰 */

    const struct aos_command *children; /* 子命令；NULL/0 代表葉節點 */
    size_t children_count;
} aos_command;

/* 外掛回給 host 的東西。 */
typedef struct aos_plugin {
    uint32_t abi_version; /* 必須等於 AOS_PLUGIN_ABI_VERSION，否則不載入 */
    const char *name;     /* 診斷用，會出現在錯誤訊息裡 */

    const aos_command *commands;
    size_t commands_count;

    /* daemon 收工時呼叫一次。可以是 NULL。 */
    void (*shutdown)(void);
} aos_plugin;

/* 外掛唯一要匯出的符號。回 NULL 代表拒絕載入（daemon 會記一行然後繼續跑）。
 * host 指標在 daemon 的整個生命週期都有效，可以存起來慢慢用。 */
const aos_plugin *aos_plugin_init(const aos_host *host);

/* 方便用：從 session 拿回 host，這樣 run() 不必自己存一份全域。 */
const aos_host *aos_host_of(aos_session *session);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AOS_PLUGIN_H */
