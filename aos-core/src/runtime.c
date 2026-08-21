#include "runtime.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_WORKERS 32

struct aos_worker {
    aos_runtime *runtime;
    const char *name;
    aos_worker_fn fn;
    void *user;
    pthread_t thread;
    int started;
};

struct aos_runtime {
    pthread_mutex_t mutex;
    /* 所有「等一件事發生」的地方共用這一個條件變數：收工時 broadcast 一次，
     * 全部醒來。這就是為什麼 daemon 收工是立刻的，而不是等最慢那個 sleep。 */
    pthread_cond_t wakeup;

    struct timespec started_at;
    uint64_t served;
    uint64_t active_connections;
    int stopping;

    void (*stop_hook)(void *);
    void *stop_hook_user;

    struct aos_worker workers[MAX_WORKERS];
    int worker_count;
};

static void now_monotonic(struct timespec *out) {
    clock_gettime(CLOCK_MONOTONIC, out);
}

aos_runtime *aos_runtime_new(void) {
    aos_runtime *runtime = calloc(1, sizeof *runtime);
    if (runtime == NULL) {
        return NULL;
    }
    if (pthread_mutex_init(&runtime->mutex, NULL) != 0) {
        free(runtime);
        return NULL;
    }
    /* 這個 cond 要配 CLOCK_MONOTONIC，否則使用者調系統時間會讓等待時間跳掉。 */
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    int failed = pthread_cond_init(&runtime->wakeup, &attr);
    pthread_condattr_destroy(&attr);
    if (failed != 0) {
        pthread_mutex_destroy(&runtime->mutex);
        free(runtime);
        return NULL;
    }
    now_monotonic(&runtime->started_at);
    return runtime;
}

void aos_runtime_lock(aos_runtime *runtime) {
    pthread_mutex_lock(&runtime->mutex);
}

void aos_runtime_unlock(aos_runtime *runtime) {
    pthread_mutex_unlock(&runtime->mutex);
}

void aos_runtime_free(aos_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }
    aos_runtime_request_stop(runtime);

    /* worker 陣列在 daemon 收工後不再變動，所以這裡讀 worker_count 不必持鎖；
     * 而且**一定不能持鎖 join**——worker 醒來的第一件事就是搶這把鎖。 */
    for (int i = 0; i < runtime->worker_count; ++i) {
        if (runtime->workers[i].started) {
            pthread_join(runtime->workers[i].thread, NULL);
        }
    }
    pthread_cond_destroy(&runtime->wakeup);
    pthread_mutex_destroy(&runtime->mutex);
    free(runtime);
}

uint64_t aos_runtime_uptime_seconds(aos_runtime *runtime) {
    struct timespec now;
    now_monotonic(&now);
    pthread_mutex_lock(&runtime->mutex);
    time_t seconds = now.tv_sec - runtime->started_at.tv_sec;
    if (now.tv_nsec < runtime->started_at.tv_nsec) {
        seconds -= 1;
    }
    pthread_mutex_unlock(&runtime->mutex);
    return seconds < 0 ? 0 : (uint64_t)seconds;
}

uint64_t aos_runtime_served(aos_runtime *runtime) {
    pthread_mutex_lock(&runtime->mutex);
    uint64_t value = runtime->served;
    pthread_mutex_unlock(&runtime->mutex);
    return value;
}

void aos_runtime_count_request(aos_runtime *runtime) {
    pthread_mutex_lock(&runtime->mutex);
    runtime->served += 1;
    pthread_mutex_unlock(&runtime->mutex);
}

uint64_t aos_runtime_active_connections(aos_runtime *runtime) {
    pthread_mutex_lock(&runtime->mutex);
    uint64_t value = runtime->active_connections;
    pthread_mutex_unlock(&runtime->mutex);
    return value;
}

void aos_runtime_connection_opened(aos_runtime *runtime) {
    pthread_mutex_lock(&runtime->mutex);
    runtime->active_connections += 1;
    pthread_mutex_unlock(&runtime->mutex);
}

void aos_runtime_connection_closed(aos_runtime *runtime) {
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->active_connections > 0) {
        runtime->active_connections -= 1;
    }
    pthread_mutex_unlock(&runtime->mutex);
}

void aos_runtime_set_stop_hook(aos_runtime *runtime, void (*hook)(void *),
                               void *user) {
    pthread_mutex_lock(&runtime->mutex);
    runtime->stop_hook = hook;
    runtime->stop_hook_user = user;
    pthread_mutex_unlock(&runtime->mutex);
}

void aos_runtime_request_stop(aos_runtime *runtime) {
    pthread_mutex_lock(&runtime->mutex);
    int already = runtime->stopping;
    runtime->stopping = 1;
    void (*hook)(void *) = runtime->stop_hook;
    void *user = runtime->stop_hook_user;
    /* 叫醒所有在 aos_worker_sleep_ms 裡等的人。 */
    pthread_cond_broadcast(&runtime->wakeup);
    pthread_mutex_unlock(&runtime->mutex);

    /* hook 在鎖外面呼叫：它會去關 acceptor（碰 syscall），
     * 持鎖做那種事只會讓別人跟著等。 */
    if (!already && hook != NULL) {
        hook(user);
    }
}

int aos_runtime_stopping(aos_runtime *runtime) {
    pthread_mutex_lock(&runtime->mutex);
    int value = runtime->stopping;
    pthread_mutex_unlock(&runtime->mutex);
    return value;
}

static void *worker_main(void *raw) {
    struct aos_worker *self = raw;
    self->fn(self->runtime, self, self->user);
    return NULL;
}

int aos_worker_spawn(aos_runtime *runtime, const char *name, aos_worker_fn fn,
                     void *user) {
    pthread_mutex_lock(&runtime->mutex);
    if (runtime->worker_count >= MAX_WORKERS || runtime->stopping) {
        pthread_mutex_unlock(&runtime->mutex);
        return -1;
    }
    struct aos_worker *slot = &runtime->workers[runtime->worker_count];
    slot->runtime = runtime;
    slot->name = name;
    slot->fn = fn;
    slot->user = user;
    slot->started = 0;
    runtime->worker_count += 1;
    pthread_mutex_unlock(&runtime->mutex);

    if (pthread_create(&slot->thread, NULL, worker_main, slot) != 0) {
        fprintf(stderr, "aos-daemon：背景工作 %s 開不起來\n", name);
        return -1;
    }
    slot->started = 1;
    return 0;
}

int aos_worker_stopping(aos_worker *self) {
    return aos_runtime_stopping(self->runtime);
}

int aos_worker_sleep_ms(aos_worker *self, unsigned ms) {
    aos_runtime *runtime = self->runtime;
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += (time_t)(ms / 1000u);
    deadline.tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&runtime->mutex);
    /* 迴圈是必要的：pthread_cond_timedwait 可能無故醒來（spurious wakeup），
     * 醒來不代表真的有人叫我們。 */
    while (!runtime->stopping) {
        int result = pthread_cond_timedwait(&runtime->wakeup, &runtime->mutex,
                                            &deadline);
        if (result == ETIMEDOUT) {
            break;
        }
    }
    int stopping = runtime->stopping;
    pthread_mutex_unlock(&runtime->mutex);
    return stopping;
}
