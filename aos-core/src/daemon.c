#include "daemon.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "frame.h"
#include "modules.h"
#include "plugin.h"
#include "registry.h"
#include "session.h"

/* accept 迴圈要看得到它才能被叫醒；收工就是把它關掉。 */
static int listen_fd = -1;
static char bound_path[512];

/* ── socket 檔的準備與清理 ── */

/* 路徑上已經有東西時：連連看。連得上代表另一個 daemon 還活著，
 * 這時候把它 unlink 掉會讓那個 daemon 從此收不到任何連線，而且完全沒有徵兆。 */
static int another_daemon_is_alive(const char *path) {
    int probe = socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0) {
        return 0;
    }
    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof address.sun_path, "%s", path);

    int alive = connect(probe, (struct sockaddr *)&address, sizeof address) == 0;
    close(probe);
    return alive;
}

static int prepare_socket(const char *path) {
    struct stat existing;
    if (stat(path, &existing) == 0) {
        if (another_daemon_is_alive(path)) {
            fprintf(stderr, "aos-daemon：%s 上已經有一個 daemon 在跑了\n", path);
            return -1;
        }
        /* 死掉的殘檔，清掉重來。 */
        unlink(path);
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("aos-daemon：socket");
        return -1;
    }

    struct sockaddr_un address;
    memset(&address, 0, sizeof address);
    address.sun_family = AF_UNIX;
    if ((size_t)snprintf(address.sun_path, sizeof address.sun_path, "%s",
                         path) >= sizeof address.sun_path) {
        fprintf(stderr, "aos-daemon：socket 路徑太長（上限 %zu）：%s\n",
                aos_socket_path_limit(), path);
        close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        perror("aos-daemon：bind");
        close(fd);
        return -1;
    }
    /* 0600：只有自己連得進來。這條 socket 可以叫起任意命令，
     * 權限開大等於把 shell 送給同機的其他使用者。 */
    if (chmod(path, 0600) != 0) {
        perror("aos-daemon：chmod");
        close(fd);
        unlink(path);
        return -1;
    }
    if (listen(fd, 16) != 0) {
        perror("aos-daemon：listen");
        close(fd);
        unlink(path);
        return -1;
    }
    return fd;
}

/* ── 收工 ── */

/* signal handler 和 stop hook 都只往這裡寫一個位元組，
 * 真正的動作由 accept 迴圈在正常的執行緒環境裡做。 */
static int signal_pipe[2] = {-1, -1};

/* ── 誰可以碰 listen_fd ──
 *
 * **只有 accept 迴圈那條執行緒**。這條規則是被 thread sanitizer 逼出來的：
 * 原本 stop hook 直接把 listen_fd 關掉，但那個 hook 是從**連線的執行緒**
 * 呼叫的（`aos daemon stop` 就是一個命令），而 accept 迴圈同時在讀它。
 *
 * 那不只是理論上的 race：fd 編號會被重用，所以「A 執行緒關掉 fd 5、
 * B 執行緒剛好開了新的 fd 5」之後，任何一邊都可能對錯的東西動手。
 *
 * 所以收工改成只送一個訊號過去，真正的 close 由擁有者自己做。 */
static void wake_accept_loop(void *ignored) {
    (void)ignored;
    /* write 是 async-signal-safe 的，所以 signal handler 也共用這個做法。 */
    ssize_t written = write(signal_pipe[1], "x", 1);
    (void)written;
}

/* 只在 accept 迴圈那條執行緒上呼叫。 */
static void close_listener(void) {
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
}

/* signal handler 裡**只有 async-signal-safe 的函式能用**，而 mutex 不是。
 * 在 handler 裡直接鎖 runtime 的話，如果剛好打斷了持有那把鎖的執行緒，
 * 就會自己等自己，daemon 從此凍住，而且現場什麼線索都沒有。
 * 所以它跟 stop hook 走同一條路：只寫一個位元組。 */
static void on_signal(int number) {
    (void)number;
    wake_accept_loop(NULL);
}

/* ── 一條連線一條執行緒 ── */

/* 同時最多幾條連線。上限的用意不是效能，是**別讓一個跑掉的迴圈把機器拖垮**：
 * 沒有上限的話 `while true; do aos ping & done` 會一路開執行緒直到系統開不出來，
 * 那時候你連 `aos daemon stop` 都送不進去。 */
#define MAX_CONNECTIONS 128

/* 每條連線的執行緒堆疊。預設是 8 MiB，128 條就是 1 GiB 的虛擬位址空間——
 * 在 64 位元的桌機上只是數字，但在嵌入式或有 overcommit 限制的地方會真的失敗。
 * 命令本身不該用到深遞迴，256 KiB 綽綽有餘。 */
#define CONNECTION_STACK_SIZE ((size_t)256 * 1024)

typedef struct {
    int fd;
    aos_runtime *runtime;
} connection_job;

static void *connection_main(void *raw) {
    connection_job *job = raw;
    aos_serve_connection(job->fd, job->runtime);
    close(job->fd);
    /* 這一行一定要在最後：收工那邊是看這個計數決定「可以拆東西了嗎」。 */
    aos_runtime_connection_closed(job->runtime);
    free(job);
    return NULL;
}

/* 連線太多時的回覆。不讀對方的 request 也沒關係——client 是照訊框收的，
 * 它會印出這段 stderr 然後拿到 exit 2，而不是看到一個沒頭沒尾的斷線。 */
static void reject_connection(int fd) {
    const char *message = "aos-daemon：同時連線數已達上限，請稍後再試\n";
    aos_write_stream(fd, AOS_FRAME_STDERR_CHUNK, message, strlen(message));
    unsigned char payload[4];
    aos_encode_exit(payload, 2);
    aos_write_frame(fd, AOS_FRAME_EXIT, payload, sizeof payload);
    close(fd);
}

int aos_run_daemon(const char *path) {
    /* client 中途離開時，寫進斷掉的 socket 會發 SIGPIPE，預設行為是**殺掉行程**。
     * asio 以前幫我們擋掉了，這裡要自己來——不擋的話 client 按個 Ctrl-C
     * 就能讓整個 daemon 消失。 */
    signal(SIGPIPE, SIG_IGN);

    listen_fd = prepare_socket(path);
    if (listen_fd < 0) {
        return 1;
    }
    snprintf(bound_path, sizeof bound_path, "%s", path);

    aos_runtime *runtime = aos_runtime_new();
    if (runtime == NULL) {
        close_listener();
        unlink(bound_path);
        return 1;
    }
    aos_runtime_set_stop_hook(runtime, wake_accept_loop, NULL);

    if (pipe(signal_pipe) != 0) {
        perror("aos-daemon：pipe");
        aos_runtime_free(runtime);
        close_listener();
        unlink(bound_path);
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* 編進來的模組先接，這樣撞名時它贏——它跟 aos-core 一起編，
     * 比較不會是意外；外掛是後來丟進來的，比較可能是誤放。 */
    aos_modules_load();
    aos_plugins_load();

    /* ── 要加背景常駐工作就加在這裡 ──
     *
     *     aos_worker_spawn(runtime, "tick", my_tick, NULL);
     *
     * 寫法與注意事項見 runtime.h。重點只有一個：迴圈裡用
     * aos_worker_sleep_ms() 而不是 sleep()，否則 daemon 收工要等它睡完。 */

    /* 連線執行緒共用這份設定：小堆疊 + detach。 */
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, CONNECTION_STACK_SIZE);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

    while (!aos_runtime_stopping(runtime)) {
        /* 同時看兩件事：有人來連，或者收到了 signal。
         * 直接 accept 的話 signal 只能靠 EINTR 打斷它，而 EINTR 會不會發生
         * 取決於 SA_RESTART，那是很容易搞錯又很難測的地方。 */
        struct pollfd watch[2];
        watch[0].fd = listen_fd;
        watch[0].events = POLLIN;
        watch[0].revents = 0;
        watch[1].fd = signal_pipe[0];
        watch[1].events = POLLIN;
        watch[1].revents = 0;

        if (poll(watch, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (watch[1].revents & POLLIN) {
            /* 有人要收工：SIGINT／SIGTERM，或某條連線跑了 `aos daemon stop`。
             * 現在是正常的執行緒環境，鎖得起來。 */
            char drained[64];
            ssize_t ignored = read(signal_pipe[0], drained, sizeof drained);
            (void)ignored;
            break;
        }
        if (!(watch[0].revents & POLLIN)) {
            continue;
        }

        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) {
                continue;
            }
            break; /* listen fd 被關掉了：收工 */
        }

        if (aos_runtime_active_connections(runtime) >= MAX_CONNECTIONS) {
            reject_connection(fd);
            continue;
        }

        connection_job *job = malloc(sizeof *job);
        if (job == NULL) {
            close(fd);
            continue;
        }
        job->fd = fd;
        job->runtime = runtime;

        /* **先加計數再開執行緒**：反過來的話，執行緒可能還沒跑到 opened()，
         * 收工那邊就已經看到 0 而開始拆東西了。 */
        aos_runtime_connection_opened(runtime);

        pthread_t thread;
        if (pthread_create(&thread, &attributes, connection_main, job) != 0) {
            fprintf(stderr, "aos-daemon：開不了執行緒，這條連線放棄\n");
            aos_runtime_connection_closed(runtime);
            close(fd);
            free(job);
            continue;
        }
    }
    pthread_attr_destroy(&attributes);

    /* 停止接受新連線。listen_fd 從頭到尾只有這條執行緒碰過。 */
    close_listener();
    /* 這是冪等的。用 signal 進來時 stopping 還沒設，背景工作要靠它叫醒。 */
    aos_runtime_request_stop(runtime);

    int still_running = 1;
    for (int waited = 0; waited < 100; ++waited) { /* 最多等 10 秒 */
        if (aos_runtime_active_connections(runtime) == 0) {
            still_running = 0;
            break;
        }
        struct timespec pause = {0, 100L * 1000 * 1000};
        nanosleep(&pause, NULL);
    }

    if (still_running) {
        /* 還有連線沒做完。**這時候絕對不能拆東西**：
         * dlclose 會把外掛的程式碼從記憶體移掉，而某條執行緒可能正在裡面跑；
         * destroy mutex 也一樣，還有人要用它。
         * 行程馬上就要結束了，作業系統會把一切收乾淨，所以什麼都不做才是對的。 */
        fprintf(stderr,
                "aos-daemon：還有 %llu 條連線沒結束，直接離開（交給系統回收）\n",
                (unsigned long long)aos_runtime_active_connections(runtime));
        unlink(bound_path);
        return 0;
    }

    /* 沒有人在跑了，可以安心拆。 */
    aos_plugins_unload();
    aos_modules_unload();
    aos_runtime_free(runtime);
    unlink(bound_path);
    return 0;
}
