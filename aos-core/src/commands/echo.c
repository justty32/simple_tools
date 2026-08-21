#include "builtin.h"

/* 邊讀邊寫，任何時刻只有一塊資料在記憶體裡。
 * 這是驗證串流是否真的有效的最小命令：`tail -f x | aos echo` 應該即時有輸出，
 * 而且 `cat 大檔 | aos echo` 不會因為超過訊框上限而失敗。 */
int aos_cmd_echo(aos_session *session, int argc, const char *const *argv,
                 void *user) {
    (void)argc;
    (void)argv;
    (void)user;

    char chunk[64 * 1024];
    for (;;) {
        size_t got = 0;
        int state = aos_read_input(session, chunk, sizeof chunk, &got);
        if (state == AOS_EOF) {
            return 0;
        }
        if (state != AOS_OK) {
            return 1;
        }
        if (aos_write_output(session, chunk, got) != AOS_OK) {
            return 1;
        }
    }
}
