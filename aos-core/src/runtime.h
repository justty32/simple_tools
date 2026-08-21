#ifndef AOS_RUNTIME_H
#define AOS_RUNTIME_H
/* runtime.h — daemon 從啟動到收工都活著的共用狀態，以及**背景常駐工作**。
 *
 * 每條連線拿到的都是同一個 aos_runtime，所以放在這裡的東西可以跨多次 CLI 呼叫
 * 存活——那正是常駐 daemon 的意義。
 *
 * ## 這裡有鎖，而且你一定會需要它
 *
 * C++ 版是單執行緒事件迴圈，所以 Runtime 沒有鎖。這一版**一條連線一條執行緒**，
 * 所以任何跨連線的狀態都必須鎖。下面每個函式都已經鎖好了；你自己加欄位的話，
 * 照著同一個 mutex 走（aos_runtime_lock / unlock），不要另外開一把——
 * 兩把鎖就有兩把鎖的順序問題。
 *
 * ## 加一個背景工作
 *
 * 想要定時清理、看門狗、批次佇列這類「跟任何一次呼叫都無關」的東西，就開一個 worker：
 *
 *     static void tick(aos_runtime *rt, aos_worker *self, void *user) {
 *         while (!aos_worker_sleep_ms(self, 60000)) {   // 回 1 = 該收工了
 *             do_the_periodic_thing(rt);
 *         }
 *     }
 *     aos_worker_spawn(runtime, "tick", tick, NULL);
 *
 * **一定要用 aos_worker_sleep_ms 而不是 sleep()**。它是可被打斷的：收工時
 * 會立刻醒來回 1。用 sleep(60) 的話，daemon 收工要等最多 60 秒才結束，
 * 而且很難聯想到是這裡造成的。
 *
 * worker 在 daemon 收工時會被通知並 join，所以它用到的東西在它返回前都還活著。
 */

#include <stdint.h>

typedef struct aos_runtime aos_runtime;
typedef struct aos_worker aos_worker;

aos_runtime *aos_runtime_new(void);
/* 通知所有 worker 收工、join 它們、釋放 runtime。 */
void aos_runtime_free(aos_runtime *runtime);

uint64_t aos_runtime_uptime_seconds(aos_runtime *runtime);
uint64_t aos_runtime_served(aos_runtime *runtime);
void aos_runtime_count_request(aos_runtime *runtime);

/* 目前有幾條連線正在處理中（不含背景 worker）。 */
uint64_t aos_runtime_active_connections(aos_runtime *runtime);
void aos_runtime_connection_opened(aos_runtime *runtime);
void aos_runtime_connection_closed(aos_runtime *runtime);

/* ── 收工 ──
 * 「收工」= 停止接受新連線 + 叫醒所有 worker。已經在跑的連線會做完，
 * 不是硬砍。所以 aos daemon stop 之後那一次呼叫自己還能把回應寫完。 */
void aos_runtime_request_stop(aos_runtime *runtime);
int aos_runtime_stopping(aos_runtime *runtime);

/* 由 daemon.c 填入：真正去關掉 acceptor 的動作。命令不該直接碰 acceptor。 */
void aos_runtime_set_stop_hook(aos_runtime *runtime, void (*hook)(void *),
                               void *user);

/* ── 背景工作 ── */
typedef void (*aos_worker_fn)(aos_runtime *runtime, aos_worker *self,
                              void *user);

/* 開一條背景執行緒。成功回 0。name 只用於診斷，要活到 daemon 結束（用字面常數）。*/
int aos_worker_spawn(aos_runtime *runtime, const char *name, aos_worker_fn fn,
                     void *user);

/* 該收工了嗎。 */
int aos_worker_stopping(aos_worker *self);

/* 可被收工打斷的 sleep。時間到回 0，被叫醒（該收工）回 1。
 * ms 給 0 就是只問一下「該收工了嗎」，不睡。 */
int aos_worker_sleep_ms(aos_worker *self, unsigned ms);

/* ── 自己加欄位時用這兩個 ── */
void aos_runtime_lock(aos_runtime *runtime);
void aos_runtime_unlock(aos_runtime *runtime);

#endif /* AOS_RUNTIME_H */
