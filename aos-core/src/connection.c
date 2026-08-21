/* connection.c — 一條連線的一生。
 *
 *   讀 request_start → 派發命令 → 把沒讀完的 stdin 吃掉 → 回 exit 訊框
 *
 * 中間任何一步壞掉都還是要走到最後那步（如果 socket 還活著），
 * 因為對方在等一個 exit code；沒等到的話它會一直卡著。 */
#include <stdio.h>
#include <string.h>

#include "daemon.h"
#include "frame.h"
#include "protocol.h"
#include "registry.h"
#include "session.h"

static void reply_exit(int fd, int code) {
    unsigned char payload[4];
    aos_encode_exit(payload, code);
    aos_write_frame(fd, AOS_FRAME_EXIT, payload, sizeof payload);
}

void aos_serve_connection(int fd, aos_runtime *runtime) {
    buf payload = BUF_INIT;
    unsigned kind = 0;

    if (aos_read_frame(fd, &kind, &payload) != 0) {
        buf_free(&payload);
        return; /* 連 request_start 都沒讀到，對方大概已經走了 */
    }
    if (kind != AOS_FRAME_REQUEST_START) {
        buf_free(&payload);
        const char *message = "連線的第一個訊框不是 request_start\n";
        aos_write_stream(fd, AOS_FRAME_STDERR_CHUNK, message, strlen(message));
        reply_exit(fd, 2);
        return;
    }

    aos_request request;
    char reason[256];
    if (aos_decode_request(payload.data, payload.len, &request, reason,
                           sizeof reason) != 0) {
        buf_free(&payload);
        buf message = BUF_INIT;
        buf_addf(&message, "%s\n", reason);
        aos_write_stream(fd, AOS_FRAME_STDERR_CHUNK, message.data, message.len);
        buf_free(&message);
        reply_exit(fd, 2);
        return;
    }
    buf_free(&payload);

    aos_session session;
    aos_session_init(&session, fd, runtime, request.working_directory);

    int code = aos_handle_command(&session, runtime, request.argc,
                                  (const char *const *)request.argv);

    /* 命令沒讀完的 stdin 要吃掉再回 exit：對方還在灌資料時就收到 exit 的話，
     * 它下一次 write 會拿到 EPIPE，看起來像是命令失敗了，其實只是我們太早收線。
     * `aos ping < 大檔案` 就是這個情況，e2e 有一項專門測它。 */
    aos_drain_input(&session);

    reply_exit(fd, code);

    aos_session_dispose(&session);
    aos_request_free(&request);
}
