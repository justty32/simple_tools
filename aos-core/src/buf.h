#ifndef AOS_BUF_H
#define AOS_BUF_H
/* buf.h — 會長大的位元組緩衝。
 *
 * C 沒有 std::string，而這個程式從頭到尾都在搬「可能含 \0 的位元組」，
 * 所以與其到處手動 realloc，不如把那件事做對一次。
 *
 * **不是字串**：不保證 NUL 結尾（要的話呼叫 buf_cstr）。長度永遠看 len。
 *
 * 配置失敗一律回 -1 並把 buf 標成壞掉，之後所有操作都是 no-op ——
 * 這樣呼叫端可以連續做十件事、最後檢查一次，不必每行都寫 if。 */

#include <stdarg.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int failed; /* 一旦配置失敗就設起來，之後全部 no-op */
} buf;

#define BUF_INIT {NULL, 0, 0, 0}

void buf_free(buf *b);

/* 全部回 0 成功、-1 失敗（失敗之後 b->failed 是 1）。 */
int buf_add(buf *b, const void *data, size_t len);
int buf_addc(buf *b, char c);
int buf_adds(buf *b, const char *text); /* NUL 結尾的字串 */
int buf_addf(buf *b, const char *fmt, ...);

/* 補一個 NUL（不計入 len），讓 data 可以當 C 字串用。 */
int buf_cstr(buf *b);

/* 把前 n 個位元組丟掉，剩下的往前搬。 */
void buf_drop_front(buf *b, size_t n);

#endif /* AOS_BUF_H */
