#include "buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void buf_free(buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = 0;
}

/* 確保還能再放 extra 個位元組。倍增成長，避免逐段 append 變成 O(n²)。 */
static int reserve(buf *b, size_t extra) {
    if (b->failed) {
        return -1;
    }
    /* +1 是留給 buf_cstr 的那個 NUL，這樣它永遠不必再配置一次。 */
    size_t needed = b->len + extra + 1;
    if (needed <= b->cap) {
        return 0;
    }
    size_t want = b->cap ? b->cap : 64;
    while (want < needed) {
        if (want > (size_t)-1 / 2) { /* 溢位防護 */
            b->failed = 1;
            return -1;
        }
        want *= 2;
    }
    char *grown = realloc(b->data, want);
    if (grown == NULL) {
        b->failed = 1;
        return -1;
    }
    b->data = grown;
    b->cap = want;
    return 0;
}

int buf_add(buf *b, const void *data, size_t len) {
    if (len == 0) {
        return b->failed ? -1 : 0;
    }
    if (reserve(b, len) != 0) {
        return -1;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    return 0;
}

int buf_addc(buf *b, char c) { return buf_add(b, &c, 1); }

int buf_adds(buf *b, const char *text) {
    return text == NULL ? 0 : buf_add(b, text, strlen(text));
}

int buf_addf(buf *b, const char *fmt, ...) {
    if (b->failed) {
        return -1;
    }
    /* 先問要多長，再一次配足。分兩次呼叫 vsnprintf 是 C99 的標準做法。 */
    va_list probe;
    va_start(probe, fmt);
    int needed = vsnprintf(NULL, 0, fmt, probe);
    va_end(probe);

    if (needed < 0) {
        b->failed = 1;
        return -1;
    }
    if (reserve(b, (size_t)needed) != 0) {
        return -1;
    }

    va_list write;
    va_start(write, fmt);
    /* cap 一定夠：reserve 已經多留了那個 +1 給結尾的 NUL。 */
    vsnprintf(b->data + b->len, (size_t)needed + 1, fmt, write);
    va_end(write);

    b->len += (size_t)needed;
    return 0;
}

int buf_cstr(buf *b) {
    if (reserve(b, 0) != 0) {
        return -1;
    }
    b->data[b->len] = '\0';
    return 0;
}

void buf_drop_front(buf *b, size_t n) {
    if (n >= b->len) {
        b->len = 0;
        return;
    }
    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
}
