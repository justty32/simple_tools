/* session 與 runtime 的檢查。用真的 socketpair，但不起 daemon、不開網路。
 *
 * 這裡驗的是兩件單元測試不容易涵蓋、又最容易寫錯的事：
 *   1. 命令真的是**邊讀邊寫**（不是收完才處理）
 *   2. 背景工作的 sleep 真的**可以被收工打斷** */
#include "check.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "commands/builtin.h"
#include "frame.h"
#include "protocol.h"
#include "runtime.h"
#include "session.h"

/* ── 假的呼叫端：住在另一條執行緒上，跟真的 CLI 走一樣的訊框 ── */

typedef struct {
    int fd;
    /* 依序記下發生了什麼，最後比對整串。 */
    char log[4096];
    size_t log_len;
} peer;

static void note(peer *p, const char *text) {
    size_t len = strlen(text);
    if (p->log_len + len + 1 < sizeof p->log) {
        memcpy(p->log + p->log_len, text, len);
        p->log_len += len;
        p->log[p->log_len++] = '|';
        p->log[p->log_len] = '\0';
    }
}

static void send_stdin(peer *p, const char *text) {
    aos_write_frame(p->fd, AOS_FRAME_STDIN_CHUNK, text, strlen(text));
}

/* 收一個 stdout 訊框並記下來。 */
static void expect_output(peer *p) {
    buf payload = BUF_INIT;
    unsigned kind = 0;
    if (aos_read_frame(p->fd, &kind, &payload) != 0) {
        note(p, "讀不到");
        buf_free(&payload);
        return;
    }
    buf_cstr(&payload);
    note(p, payload.data);
    buf_free(&payload);
}

/* 這條執行緒證明 echo 是串流的：它送一塊、**馬上**等一塊回來。
 * 如果 echo 是先收完 stdin 才處理，這裡第一個 expect_output 就會永遠等下去。 */
static void *streaming_peer(void *raw) {
    peer *p = raw;
    const char *words[] = {"one", "two", "three"};
    for (int i = 0; i < 3; ++i) {
        send_stdin(p, words[i]);
        note(p, "送出");
        expect_output(p);
    }
    aos_write_frame(p->fd, AOS_FRAME_STDIN_END, NULL, 0);
    return NULL;
}

static void test_echo_reads_and_writes_one_chunk_at_a_time(void) {
    int pair[2];
    AOS_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);

    peer p;
    memset(&p, 0, sizeof p);
    p.fd = pair[0];

    pthread_t thread;
    pthread_create(&thread, NULL, streaming_peer, &p);

    aos_runtime *runtime = aos_runtime_new();
    aos_session session;
    aos_session_init(&session, pair[1], runtime, "/tmp");
    AOS_CHECK(aos_cmd_echo(&session, 0, NULL, NULL) == 0);

    pthread_join(thread, NULL);

    /* 交錯才是對的。變成「送出|送出|送出|one|two|three」就代表它先收完才吐。 */
    AOS_CHECK(strcmp(p.log, "送出|one|送出|two|送出|three|") == 0);

    aos_session_dispose(&session);
    aos_runtime_free(runtime);
    close(pair[0]);
    close(pair[1]);
}

/* ── 命令用比訊框小的 buffer 讀，不該掉資料 ── */

static void *send_one_big_chunk(void *raw) {
    peer *p = raw;
    char big[5000];
    memset(big, 'x', sizeof big);
    aos_write_frame(p->fd, AOS_FRAME_STDIN_CHUNK, big, sizeof big);
    aos_write_frame(p->fd, AOS_FRAME_STDIN_END, NULL, 0);
    return NULL;
}

static void test_small_reads_do_not_lose_bytes(void) {
    int pair[2];
    AOS_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);

    peer p;
    memset(&p, 0, sizeof p);
    p.fd = pair[0];
    pthread_t thread;
    pthread_create(&thread, NULL, send_one_big_chunk, &p);

    aos_runtime *runtime = aos_runtime_new();
    aos_session session;
    aos_session_init(&session, pair[1], runtime, "/tmp");

    /* 一次只讀 64 個位元組，訊框卻有 5000 個。 */
    size_t total = 0;
    for (;;) {
        char small[64];
        size_t got = 0;
        int state = aos_read_input(&session, small, sizeof small, &got);
        if (state == AOS_EOF) {
            break;
        }
        AOS_CHECK(state == AOS_OK);
        total += got;
    }
    AOS_CHECK(total == 5000);

    pthread_join(thread, NULL);
    aos_session_dispose(&session);
    aos_runtime_free(runtime);
    close(pair[0]);
    close(pair[1]);
}

/* ── ping 完全不碰 stdin ── */

static void test_ping_ignores_stdin(void) {
    int pair[2];
    AOS_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);

    aos_runtime *runtime = aos_runtime_new();
    aos_session session;
    aos_session_init(&session, pair[1], runtime, "/tmp");

    /* 一個字都沒送進來，ping 仍然要立刻回答。會去讀 stdin 的話這裡就卡死了。 */
    AOS_CHECK(aos_cmd_ping(&session, 0, NULL, NULL) == 0);

    buf payload = BUF_INIT;
    unsigned kind = 0;
    AOS_CHECK(aos_read_frame(pair[0], &kind, &payload) == 0);
    AOS_CHECK(kind == AOS_FRAME_STDOUT_CHUNK);
    buf_cstr(&payload);
    AOS_CHECK(strcmp(payload.data, "pong\n") == 0);
    buf_free(&payload);

    aos_session_dispose(&session);
    aos_runtime_free(runtime);
    close(pair[0]);
    close(pair[1]);
}

/* ── 大的寫入會自動切成多個訊框 ── */

typedef struct {
    int fd;
    const char *data;
    size_t len;
} stream_job;

static void *stream_writer(void *raw) {
    stream_job *job = raw;
    aos_write_stream(job->fd, AOS_FRAME_STDOUT_CHUNK, job->data, job->len);
    return NULL;
}

static void test_write_stream_splits(void) {
    int pair[2];
    AOS_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);

    size_t total = AOS_CHUNK_SIZE + 1234;
    char *big = malloc(total);
    AOS_CHECK(big != NULL);
    if (big == NULL) {
        return;
    }
    memset(big, 'y', total);

    /* 寫入量超過 socket 緩衝，所以寫的人要在另一條執行緒上，
     * 不然這裡寫、這裡讀，會自己等自己。 */
    stream_job job = {pair[1], big, total};
    pthread_t writer_thread;
    pthread_create(&writer_thread, NULL, stream_writer, &job);

    size_t received = 0;
    int frames = 0;
    while (received < total) {
        buf payload = BUF_INIT;
        unsigned kind = 0;
        AOS_CHECK(aos_read_frame(pair[0], &kind, &payload) == 0);
        AOS_CHECK(kind == AOS_FRAME_STDOUT_CHUNK);
        AOS_CHECK(payload.len <= AOS_CHUNK_SIZE);
        received += payload.len;
        frames += 1;
        buf_free(&payload);
    }
    AOS_CHECK(received == total);
    AOS_CHECK(frames == 2); /* 一塊滿的 + 一塊剩下的 */

    pthread_join(writer_thread, NULL);
    free(big);
    close(pair[0]);
    close(pair[1]);
}

/* ── 背景工作的 sleep 可以被收工打斷 ── */

static void slow_worker(aos_runtime *runtime, aos_worker *self, void *user) {
    (void)runtime;
    int *woke = user;
    /* 睡 10 秒。收工時必須立刻醒來，而不是真的睡滿。 */
    *woke = aos_worker_sleep_ms(self, 10000);
}

static void test_worker_sleep_is_interruptible(void) {
    aos_runtime *runtime = aos_runtime_new();
    int woke = -1;
    AOS_CHECK(aos_worker_spawn(runtime, "slow", slow_worker, &woke) == 0);

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    struct timespec settle = {0, 50 * 1000 * 1000};
    nanosleep(&settle, NULL); /* 讓 worker 真的進到 sleep 裡 */

    aos_runtime_free(runtime); /* 內含 request_stop + join */

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    /* 沒有可打斷的 sleep 的話這裡會是 10000ms，daemon 收工就要等十秒。 */
    AOS_CHECK(elapsed_ms < 1000);
    AOS_CHECK(woke == 1); /* 回 1 代表「該收工了」，不是時間到 */
}

static void test_runtime_counters(void) {
    aos_runtime *runtime = aos_runtime_new();
    AOS_CHECK(aos_runtime_served(runtime) == 0);
    aos_runtime_count_request(runtime);
    aos_runtime_count_request(runtime);
    AOS_CHECK(aos_runtime_served(runtime) == 2);

    AOS_CHECK(aos_runtime_active_connections(runtime) == 0);
    aos_runtime_connection_opened(runtime);
    AOS_CHECK(aos_runtime_active_connections(runtime) == 1);
    aos_runtime_connection_closed(runtime);
    AOS_CHECK(aos_runtime_active_connections(runtime) == 0);
    /* 多關一次不該讓它變成天文數字（unsigned 減到負的）。 */
    aos_runtime_connection_closed(runtime);
    AOS_CHECK(aos_runtime_active_connections(runtime) == 0);

    AOS_CHECK(!aos_runtime_stopping(runtime));
    aos_runtime_request_stop(runtime);
    AOS_CHECK(aos_runtime_stopping(runtime));
    aos_runtime_free(runtime);
}

int main(void) {
    test_echo_reads_and_writes_one_chunk_at_a_time();
    test_small_reads_do_not_lose_bytes();
    test_ping_ignores_stdin();
    test_write_stream_splits();
    test_worker_sleep_is_interruptible();
    test_runtime_counters();
    return aos_report();
}
