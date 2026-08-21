/* bin/aos-daemon —— 取 socket 路徑，然後跑起來。就這樣。 */
#include <stdio.h>

#include "daemon.h"
#include "socket_path.h"

int main(void) {
    char path[4096];
    if (aos_socket_path(path, sizeof path) != 0) {
        fprintf(stderr, "aos-daemon：socket 路徑太長\n");
        return 1;
    }
    return aos_run_daemon(path);
}
