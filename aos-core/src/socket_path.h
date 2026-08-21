#ifndef AOS_SOCKET_PATH_H
#define AOS_SOCKET_PATH_H
/* socket_path.h — daemon 和 CLI 用同一套規則決定 socket 在哪，
 * 所以兩邊一定會碰頭：
 *
 *   1. 環境變數 AOS_SOCKET
 *   2. $XDG_RUNTIME_DIR/aos.sock
 *   3. /tmp/aos-<uid>.sock
 */

#include <stddef.h>

/* 成功回 0；路徑塞不進 cap 回 -1。 */
int aos_socket_path(char *out, size_t cap);

/* sockaddr_un.sun_path 的大小（一般是 108）。Unix socket 路徑很短，
 * 太長的話 bind 會失敗，而錯誤訊息通常看不出是長度問題。 */
size_t aos_socket_path_limit(void);

#endif /* AOS_SOCKET_PATH_H */
