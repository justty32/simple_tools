#include "builtin.h"

#include "../registry.h"

/* 走 stdout，因為這是使用者明確要求的正常輸出。
 * 「你沒給命令」那種提示走 stderr，兩者共用同一份渲染。 */
int aos_cmd_help(aos_session *session, int argc, const char *const *argv,
                 void *user) {
    (void)argc;
    (void)argv;
    (void)user;

    size_t count = 0;
    const aos_command *table = aos_commands(&count);

    buf listing = BUF_INIT;
    aos_render_command_list(&listing, NULL, 0, table, count);
    aos_write_output(session, listing.data, listing.len);
    buf_free(&listing);
    return 0;
}
