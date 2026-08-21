/* client.c — CLI 端。
 *
 * ## 為什麼要兩條執行緒
 *
 * 「灌 stdin」和「印輸出」必須同時進行。只做一邊的話：
 *
 *   - 先把 stdin 灌完才讀輸出 → `tail -f log | aos echo` 一個字都不會出來，
 *     因為 tail 永遠不會 EOF。
 *   - 先讀輸出才灌 stdin → 命令在等 stdin，我們在等輸出，兩邊一起卡死。
 *
 * 所以：主執行緒收輸出，另一條執行緒灌 stdin。兩條都用阻塞 IO，
 * 跟 daemon 那邊同一個模型。
 *
 * ## 那條灌 stdin 的執行緒可能永遠不會結束，這是預期的
 *
 * `aos ping < 20MiB.bin` 的情況：ping 根本不讀 stdin，所以我們寫到 socket
 * 緩衝滿了就卡住。但主執行緒仍然收得到 exit 訊框，然後直接讓行程結束——
 * 那條卡住的執行緒跟著行程一起走。所以它是 detach 的，而且我們不 join 它。
 *
 * ## 一般檔案不需要特別處理
 *
 * C++ 版有兩條 stdin 路徑，因為 asio 的 epoll 收不了一般檔案。
 * 這裡用的是阻塞 read()，對 pipe、終端機、一般檔案一視同仁，所以只有一條路。 */

#include "client.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "buf.h"
#include "frame.h"
#include "protocol.h"

typedef struct {
    int fd;
} forwarder;

static void *forward_stdin(void *raw) {
    forwarder *self = raw;
    char chunk[AOS_CHUNK_SIZE];

    for (;;) {
        ssize_t got = read(STDIN_FILENO, chunk, sizeof chunk);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (got == 0) {
            break; /* EOF */
        }
        if (aos_write_frame(self->fd, AOS_FRAME_STDIN_CHUNK, chunk,
                            (size_t)got) != 0) {
            /* daemon 先收線了（例如命令根本不看 stdin）。這是正常的，不用吵。 */
            return NULL;
        }
    }
    aos_write_frame(self->fd, AOS_FRAME_STDIN_END, NULL, 0);
    return NULL;
}

static int connect_to_daemon(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("aos：socket");
        return -1;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    if ((size_t)snprintf(address.sun_path, sizeof address.sun_path, "%s",
                         path) >= sizeof address.sun_path) {
        fprintf(stderr, "aos：socket 路徑太長：%s\n", path);
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        fprintf(stderr, "aos：連不上 %s（aos-daemon 有在跑嗎？）\n", path);
        close(fd);
        return -1;
    }
    return fd;
}

int aos_run_client(int argc, const char *const *argv, const char *path) {
    /* daemon 那邊先收線時，寫進去會發 SIGPIPE 並殺掉我們。 */
    signal(SIGPIPE, SIG_IGN);

    int fd = connect_to_daemon(path);
    if (fd < 0) {
        return 1;
    }

    char cwd[4096];
    if (getcwd(cwd, sizeof cwd) == NULL) {
        cwd[0] = '\0';
    }

    buf request = BUF_INIT;
    if (aos_encode_request(&request, argc, argv, cwd) != 0 ||
        aos_write_frame(fd, AOS_FRAME_REQUEST_START, request.data,
                        request.len) != 0) {
        fprintf(stderr, "aos：送不出請求\n");
        buf_free(&request);
        close(fd);
        return 1;
    }
    buf_free(&request);

    /* stdin 是終端機的話不要等 EOF：使用者沒有要餵東西進來，
     * 等下去只會讓 `aos ping` 停在那裡等一個永遠不會來的 Ctrl-D。 */
    static forwarder job;
    job.fd = fd;
    if (isatty(STDIN_FILENO)) {
        aos_write_frame(fd, AOS_FRAME_STDIN_END, NULL, 0);
    } else {
        pthread_t thread;
        if (pthread_create(&thread, NULL, forward_stdin, &job) == 0) {
            pthread_detach(thread);
        } else {
            aos_write_frame(fd, AOS_FRAME_STDIN_END, NULL, 0);
        }
    }

    /* 收輸出，直到拿到 exit 訊框。 */
    int exit_code = 1;
    for (;;) {
        buf payload = BUF_INIT;
        unsigned kind = 0;
        if (aos_read_frame(fd, &kind, &payload) != 0) {
            buf_free(&payload);
            fprintf(stderr, "aos：daemon 在回覆完成前就斷線了\n");
            break;
        }
        if (kind == AOS_FRAME_STDOUT_CHUNK) {
            aos_write_all(STDOUT_FILENO, payload.data, payload.len);
        } else if (kind == AOS_FRAME_STDERR_CHUNK) {
            aos_write_all(STDERR_FILENO, payload.data, payload.len);
        } else if (kind == AOS_FRAME_EXIT) {
            int32_t code = 1;
            if (aos_decode_exit(payload.data, payload.len, &code) == 0) {
                exit_code = (int)code;
            }
            buf_free(&payload);
            break;
        }
        /* 認不得的訊框直接忽略：對方比我們新，但這不是我們該死掉的理由。 */
        buf_free(&payload);
    }

    /* 這裡刻意不 join 那條灌 stdin 的執行緒（見檔案開頭）。
     * 直接關 fd 也不行——它可能正卡在 write，關掉會讓它拿到 EBADF 而不是乾淨結束。
     * 交給行程結束來清理。 */
    return exit_code;
}
