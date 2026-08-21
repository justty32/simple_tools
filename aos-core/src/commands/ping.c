#include "builtin.h"

/* 不碰 stdin。這是刻意的，而且有測試守著：對方就算灌了 20 MiB 進來，
 * ping 也該立刻回答然後結束，不能為了「把 stdin 讀完」而卡住。
 * 沒讀完的部分由 connection.c 的 drain 收拾。 */
int aos_cmd_ping(aos_session *session, int argc, const char *const *argv,
                 void *user) {
    (void)argc;
    (void)argv;
    (void)user;
    aos_say(session, "pong\n");
    return 0;
}
