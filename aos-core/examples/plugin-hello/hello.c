/* hello.c — 最小可用的外掛，同時也是 ABI 真的能用的證明。
 *
 *   cmake --build build
 *   AOS_PLUGINS=$PWD/bin/plugins/hello.so ./bin/aos-daemon &
 *   ./bin/aos hello shout 你好        → 你好
 *   printf 'abc' | ./bin/aos hello upper   → ABC
 *
 * 三件事值得看：
 *   1. 命令可以有子命令，跟內建的一樣（hello 是分組節點）
 *   2. upper 是**串流**的：邊讀邊寫，不先把 stdin 收完
 *   3. 整個檔案只 include 一個標頭，而且不連 aos 的任何函式庫 */

#include "aos/plugin.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const aos_host *host;

/* 把參數喊回去。示範 argv 與 stdout。 */
static int shout(aos_session *session, int argc, const char *const *argv,
                 void *user) {
    (void)user;
    if (argc == 0) {
        const char *usage = "用法：aos hello shout <字...>\n";
        host->write_error(session, usage, strlen(usage));
        return 2; /* 回傳值就是 exit status */
    }
    for (int i = 0; i < argc; ++i) {
        if (i > 0) {
            host->write_output(session, " ", 1);
        }
        host->write_output(session, argv[i], strlen(argv[i]));
    }
    host->write_output(session, "\n", 1);
    return 0;
}

/* 把 stdin 轉大寫再吐出來。示範**串流**：讀一塊寫一塊，
 * 所以 `tail -f x | aos hello upper` 會即時有輸出，
 * 而且多大的輸入都不會把記憶體吃光。 */
static int upper(aos_session *session, int argc, const char *const *argv,
                 void *user) {
    (void)argc;
    (void)argv;
    (void)user;

    char chunk[16 * 1024];
    for (;;) {
        size_t got = 0;
        int state = host->read_input(session, chunk, sizeof chunk, &got);
        if (state == AOS_EOF) {
            return 0;
        }
        if (state != AOS_OK) {
            return 1;
        }
        for (size_t i = 0; i < got; ++i) {
            chunk[i] = (char)toupper((unsigned char)chunk[i]);
        }
        if (host->write_output(session, chunk, got) != AOS_OK) {
            return 1;
        }
    }
}

/* 示範 host 提供的環境資訊。 */
static int where(aos_session *session, int argc, const char *const *argv,
                 void *user) {
    (void)argc;
    (void)argv;
    (void)user;

    char line[4200];
    int len = snprintf(line, sizeof line, "工作目錄：%s\ndaemon 已經跑了 %llus\n",
                       host->working_directory(session),
                       (unsigned long long)host->uptime_seconds(session));
    host->write_output(session, line, (size_t)len);
    return 0;
}

/* 命令表宣告成 static：host 不會複製它，所以它必須活到 shutdown。 */
static const aos_command hello_children[] = {
    {"shout", "把參數喊回去", shout, NULL, NULL, 0},
    {"upper", "把 stdin 轉大寫（串流）", upper, NULL, NULL, 0},
    {"where", "顯示工作目錄與 daemon uptime", where, NULL, NULL, 0},
};

static const aos_command hello_commands[] = {
    {"hello", "範例外掛", NULL, NULL, hello_children,
     sizeof hello_children / sizeof hello_children[0]},
};

static const aos_plugin hello_plugin = {
    AOS_PLUGIN_ABI_VERSION,
    "hello",
    hello_commands,
    sizeof hello_commands / sizeof hello_commands[0],
    NULL, /* 沒有要收尾的東西 */
};

const aos_plugin *aos_plugin_init(const aos_host *given) {
    /* ABI 對不上就拒絕載入，daemon 會記一行然後繼續跑。 */
    if (given == NULL || given->abi_version != AOS_PLUGIN_ABI_VERSION) {
        return NULL;
    }
    host = given;
    return &hello_plugin;
}
