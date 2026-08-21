#ifndef AOS_BUILTIN_H
#define AOS_BUILTIN_H
/* builtin.h — 內建命令的宣告。
 *
 * 新增一個命令的完整步驟：
 *   1. 在 src/commands/ 加一個 .c，寫一個下面這個形狀的函式
 *   2. 在這裡加一行宣告
 *   3. 在 src/registry.c 的表裡加一列（子命令就加進對應的子表）
 * CMake 是用 glob 掃 src/，新檔案不必手動加進建置。
 *
 * 簽名跟外掛命令**完全一樣**（見 include/aos/plugin.h 的 aos_command）。
 * 差別只有一個：內建命令看得到 session->runtime，外掛看不到——
 * 所以「請 daemon 收工」這種事只有內建做得到，這是刻意的。 */

#include "../session.h"

int aos_cmd_ping(aos_session *session, int argc, const char *const *argv,
                 void *user);
int aos_cmd_echo(aos_session *session, int argc, const char *const *argv,
                 void *user);
int aos_cmd_help(aos_session *session, int argc, const char *const *argv,
                 void *user);

/* `aos daemon ...` 這一組。 */
int aos_cmd_daemon_status(aos_session *session, int argc,
                          const char *const *argv, void *user);
int aos_cmd_daemon_stop(aos_session *session, int argc, const char *const *argv,
                        void *user);
int aos_cmd_daemon_plugins(aos_session *session, int argc,
                           const char *const *argv, void *user);

/* 沒對上任何名字時的後備：把收到的東西原樣列出來，方便除錯。
 * 它拿到的是**完整的** argv（沒有去掉命令路徑），因為根本沒對上路徑。 */
int aos_cmd_describe(aos_session *session, int argc, const char *const *argv,
                     void *user);

#endif /* AOS_BUILTIN_H */
