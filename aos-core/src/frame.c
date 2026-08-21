#include "frame.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

int aos_read_exact(int fd, void *out, size_t n) {
    unsigned char *at = out;
    while (n > 0) {
        ssize_t got = read(fd, at, n);
        if (got < 0) {
            if (errno == EINTR) {
                continue; /* 被 signal 打斷，不是錯誤 */
            }
            return -1;
        }
        if (got == 0) {
            return -1; /* 對方關了，而我們還等著東西 */
        }
        at += (size_t)got;
        n -= (size_t)got;
    }
    return 0;
}

int aos_write_all(int fd, const void *data, size_t n) {
    const unsigned char *at = data;
    while (n > 0) {
        ssize_t put = write(fd, at, n);
        if (put < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        at += (size_t)put;
        n -= (size_t)put;
    }
    return 0;
}

int aos_read_frame(int fd, unsigned *kind, buf *out) {
    unsigned char header[AOS_FRAME_HEADER_SIZE];
    if (aos_read_exact(fd, header, sizeof header) != 0) {
        return -1;
    }
    uint32_t payload_len;
    aos_decode_header(header, &payload_len, kind);

    /* 先看長度再配置：不然一個壞掉的標頭就能叫我們配置 4 GiB。 */
    if (payload_len > AOS_MAX_PAYLOAD) {
        return -1;
    }
    if (payload_len == 0) {
        return 0; /* 合法：stdin_end 就沒有 payload */
    }

    size_t before = out->len;
    /* 先把空間撐出來，再直接讀進去，省一次複製。 */
    if (buf_add(out, NULL, 0) != 0) {
        return -1;
    }
    {
        /* buf 沒有「保留但不填」的介面，所以用一塊暫存讀進來再 append。
         * 一次一塊 payload，最多 8 MiB，這個複製不值得為它增加介面。 */
        char stack[8192];
        uint32_t left = payload_len;
        while (left > 0) {
            size_t want = left < sizeof stack ? left : sizeof stack;
            if (aos_read_exact(fd, stack, want) != 0) {
                out->len = before;
                return -1;
            }
            if (buf_add(out, stack, want) != 0) {
                out->len = before;
                return -1;
            }
            left -= (uint32_t)want;
        }
    }
    return 0;
}

int aos_write_frame(int fd, unsigned kind, const void *data, size_t len) {
    if (len > AOS_MAX_PAYLOAD) {
        return -1;
    }
    unsigned char header[AOS_FRAME_HEADER_SIZE];
    aos_encode_header(header, (uint32_t)len, kind);

    /* 標頭和 payload 分兩次 write 有機會被別的執行緒插進中間，
     * 所以呼叫端負責一條 fd 一次只有一個寫入者（見 session.c 的鎖）。 */
    if (aos_write_all(fd, header, sizeof header) != 0) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    return aos_write_all(fd, data, len);
}

int aos_write_stream(int fd, unsigned kind, const void *data, size_t len) {
    const unsigned char *at = data;
    while (len > 0) {
        size_t piece = len < AOS_CHUNK_SIZE ? len : AOS_CHUNK_SIZE;
        if (aos_write_frame(fd, kind, at, piece) != 0) {
            return -1;
        }
        at += piece;
        len -= piece;
    }
    return 0;
}
