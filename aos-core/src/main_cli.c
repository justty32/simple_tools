/* bin/aos —— 組裝而已，沒有任何命令語意。 */
#include <stdio.h>

#include "client.h"
#include "socket_path.h"

int main(int argc, char **argv) {
    /* argc 可能是 0（execve 允許傳空的 argv），所以不要直接算 argc - 1。 */
    int count = argc > 0 ? argc - 1 : 0;
    const char *const *arguments = argc > 0 ? (const char *const *)argv + 1
                                            : (const char *const *)argv;

    char path[4096];
    if (aos_socket_path(path, sizeof path) != 0) {
        fprintf(stderr, "aos：socket 路徑太長\n");
        return 1;
    }
    return aos_run_client(count, arguments, path);
}
