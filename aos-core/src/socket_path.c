#include "socket_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

int aos_socket_path(char *out, size_t cap) {
    const char *explicit_path = getenv("AOS_SOCKET");
    if (explicit_path != NULL && *explicit_path != '\0') {
        return snprintf(out, cap, "%s", explicit_path) < (int)cap ? 0 : -1;
    }

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != NULL && *runtime_dir != '\0') {
        return snprintf(out, cap, "%s/aos.sock", runtime_dir) < (int)cap ? 0
                                                                         : -1;
    }

    /* 最後的退路帶 uid，免得多使用者的機器上互相踩到。 */
    return snprintf(out, cap, "/tmp/aos-%u.sock", (unsigned)getuid()) < (int)cap
               ? 0
               : -1;
}

size_t aos_socket_path_limit(void) {
    struct sockaddr_un probe;
    return sizeof probe.sun_path;
}
