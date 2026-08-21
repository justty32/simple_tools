#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frame.h"

void aos_session_init(aos_session *session, int fd, aos_runtime *runtime,
                      const char *working_directory) {
    memset(session, 0, sizeof *session);
    session->fd = fd;
    session->runtime = runtime;
    session->working_directory = working_directory;
    pthread_mutex_init(&session->write_mutex, NULL);
}

void aos_session_dispose(aos_session *session) {
    free(session->pending);
    session->pending = NULL;
    pthread_mutex_destroy(&session->write_mutex);
}

/* 收下一塊 stdin 進 pending。回 0 拿到東西、1 結束了、-1 壞了。 */
static int pull_chunk(aos_session *session) {
    buf incoming = BUF_INIT;
    unsigned kind = 0;
    if (aos_read_frame(session->fd, &kind, &incoming) != 0) {
        buf_free(&incoming);
        session->broken = 1;
        return -1;
    }
    if (kind == AOS_FRAME_STDIN_END) {
        buf_free(&incoming);
        session->input_ended = 1;
        return 1;
    }
    if (kind != AOS_FRAME_STDIN_CHUNK) {
        /* 對方在 stdin 還沒結束時送了別的東西。這是協定錯誤，不是連線錯誤，
         * 但這一層沒有回覆的能力，所以只能當成壞掉，由 connection.c 收尾。 */
        buf_free(&incoming);
        session->broken = 1;
        return -1;
    }

    free(session->pending);
    session->pending = (unsigned char *)incoming.data;
    session->pending_len = incoming.len;
    session->pending_cap = incoming.cap;
    session->pending_pos = 0;
    /* buf 的所有權交出去了，不要 buf_free。 */
    return 0;
}

int aos_read_input(aos_session *session, void *out, size_t cap,
                   size_t *out_len) {
    *out_len = 0;
    if (cap == 0) {
        return AOS_OK;
    }
    if (session->broken) {
        return AOS_BROKEN;
    }

    /* 上一塊還沒交完就先交完，不要為了湊滿 cap 去多讀一塊——
     * 那會讓「邊來邊出」變成「等湊滿才出」。 */
    while (session->pending_pos >= session->pending_len) {
        if (session->input_ended) {
            return AOS_EOF;
        }
        int state = pull_chunk(session);
        if (state == 1) {
            return AOS_EOF;
        }
        if (state < 0) {
            return AOS_BROKEN;
        }
    }

    size_t available = session->pending_len - session->pending_pos;
    size_t give = available < cap ? available : cap;
    memcpy(out, session->pending + session->pending_pos, give);
    session->pending_pos += give;
    *out_len = give;
    return AOS_OK;
}

static int write_kind(aos_session *session, unsigned kind, const void *data,
                      size_t len) {
    if (session->broken) {
        return AOS_BROKEN;
    }
    if (len == 0) {
        return AOS_OK;
    }
    pthread_mutex_lock(&session->write_mutex);
    int failed = aos_write_stream(session->fd, kind, data, len);
    pthread_mutex_unlock(&session->write_mutex);
    if (failed != 0) {
        session->broken = 1;
        return AOS_BROKEN;
    }
    return AOS_OK;
}

int aos_write_output(aos_session *session, const void *data, size_t len) {
    return write_kind(session, AOS_FRAME_STDOUT_CHUNK, data, len);
}

int aos_write_error(aos_session *session, const void *data, size_t len) {
    return write_kind(session, AOS_FRAME_STDERR_CHUNK, data, len);
}

int aos_say(aos_session *session, const char *text) {
    return aos_write_output(session, text, strlen(text));
}

int aos_complain(aos_session *session, const char *text) {
    return aos_write_error(session, text, strlen(text));
}

int aos_drain_input(aos_session *session) {
    char scratch[8192];
    for (;;) {
        size_t got = 0;
        int state = aos_read_input(session, scratch, sizeof scratch, &got);
        if (state == AOS_EOF) {
            return 0;
        }
        if (state != AOS_OK) {
            return -1;
        }
    }
}

/* ── 交給外掛的那張表 ── */

static const char *host_working_directory(aos_session *session) {
    return session->working_directory;
}

static uint64_t host_uptime_seconds(aos_session *session) {
    return aos_runtime_uptime_seconds(session->runtime);
}

static void host_log(const char *message) {
    fprintf(stderr, "aos-daemon：%s\n", message);
}

static const aos_host host_table = {
    AOS_PLUGIN_ABI_VERSION,
    aos_read_input,
    aos_write_output,
    aos_write_error,
    host_working_directory,
    host_uptime_seconds,
    host_log,
};

const aos_host *aos_host_table(void) { return &host_table; }

const aos_host *aos_host_of(aos_session *session) {
    (void)session;
    return &host_table;
}
