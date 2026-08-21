#ifndef AOS_SESSION_H
#define AOS_SESSION_H
/* session.h — 命令對外的唯一管道：三條標準串流。
 *
 * 內建命令和外掛命令走的是**完全一樣的函式**（外掛只是透過 aos_host 那張
 * 函式指標表間接呼叫它們）。內建的不是特權——這樣外掛能做的事永遠不會比
 * 內建的少，也就不會有「這個功能只有寫在 core 裡才做得到」的情況。 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "../include/aos/plugin.h"
#include "buf.h"
#include "runtime.h"

struct aos_session {
    int fd;
    aos_runtime *runtime;
    const char *working_directory;

    /* 讀進來但呼叫端還沒拿完的那一塊。命令用多大的 buffer 讀是它的自由，
     * 不該被訊框大小綁住，所以中間要有這個緩衝。 */
    unsigned char *pending;
    size_t pending_len;
    size_t pending_pos;
    size_t pending_cap;

    int input_ended;
    int broken; /* 連線壞掉之後就不要再寫了，避免一路噴錯 */

    /* stdout 與 stderr 共用一條 fd，而命令可能從多條執行緒寫
     * （它自己開的那些）。沒有這把鎖的話，兩個訊框會交錯，
     * 對方就解不出來了。 */
    pthread_mutex_t write_mutex;
};

void aos_session_init(aos_session *session, int fd, aos_runtime *runtime,
                      const char *working_directory);
void aos_session_dispose(aos_session *session);

/* 這三個就是 aos_host 裡那三個，簽名一模一樣。 */
int aos_read_input(aos_session *session, void *out, size_t cap,
                   size_t *out_len);
int aos_write_output(aos_session *session, const void *data, size_t len);
int aos_write_error(aos_session *session, const void *data, size_t len);

/* 方便用的：寫一段 NUL 結尾的字串。 */
int aos_say(aos_session *session, const char *text);
int aos_complain(aos_session *session, const char *text);

/* 把命令沒讀完的 stdin 吃掉。**送 exit 之前一定要做** ——
 * 對方還在灌資料時就收到 exit 的話，它拿到的是 EPIPE 而不是乾淨結束。 */
int aos_drain_input(aos_session *session);

/* 給 plugin.c 用：那張交給外掛的函式表。 */
const aos_host *aos_host_table(void);

#endif /* AOS_SESSION_H */
