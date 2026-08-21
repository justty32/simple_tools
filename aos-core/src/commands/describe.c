#include "builtin.h"

/* 後備：命令名認不得時，把收到的東西原樣印出來。
 * 印 stdin 的**位元組數**而不是內容，因為那可能是 20 MiB 的二進位。 */
int aos_cmd_describe(aos_session *session, int argc, const char *const *argv,
                     void *user) {
    (void)user;

    buf out = BUF_INIT;
    buf_addf(&out, "認不得的命令：%s\n\n", argc > 0 ? argv[0] : "(沒有)");
    buf_addf(&out, "收到 %d 個參數：\n", argc);
    for (int i = 0; i < argc; ++i) {
        buf_addf(&out, "  [%d] %s\n", i, argv[i]);
    }
    buf_addf(&out, "工作目錄：%s\n", session->working_directory);

    size_t total = 0;
    char scratch[64 * 1024];
    for (;;) {
        size_t got = 0;
        if (aos_read_input(session, scratch, sizeof scratch, &got) != AOS_OK) {
            break;
        }
        total += got;
    }
    buf_addf(&out, "stdin：%zu 個位元組\n", total);

    aos_write_error(session, out.data, out.len);
    buf_free(&out);
    return 2;
}
