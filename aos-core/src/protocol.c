#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int aos_frame_kind_known(unsigned kind) {
    switch (kind) {
    case AOS_FRAME_REQUEST_START:
    case AOS_FRAME_STDIN_CHUNK:
    case AOS_FRAME_STDIN_END:
    case AOS_FRAME_STDOUT_CHUNK:
    case AOS_FRAME_STDERR_CHUNK:
    case AOS_FRAME_EXIT:
        return 1;
    default:
        return 0;
    }
}

/* 大端寫死，不看本機位元序：兩端有一天可能不是同一台機器。 */
static void put_u32(unsigned char *out, uint32_t value) {
    out[0] = (unsigned char)(value >> 24);
    out[1] = (unsigned char)(value >> 16);
    out[2] = (unsigned char)(value >> 8);
    out[3] = (unsigned char)value;
}

static uint32_t get_u32(const unsigned char *in) {
    return ((uint32_t)in[0] << 24) | ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) | (uint32_t)in[3];
}

void aos_encode_header(unsigned char *header, uint32_t payload_len,
                       unsigned kind) {
    put_u32(header, payload_len);
    header[4] = (unsigned char)kind;
}

void aos_decode_header(const unsigned char *header, uint32_t *payload_len,
                       unsigned *kind) {
    *payload_len = get_u32(header);
    *kind = header[4];
}

void aos_request_free(aos_request *request) {
    for (int i = 0; i < request->argc; ++i) {
        free(request->argv[i]);
    }
    free(request->argv);
    free(request->working_directory);
    request->argv = NULL;
    request->working_directory = NULL;
    request->argc = 0;
}

static int add_u32(buf *out, uint32_t value) {
    unsigned char raw[4];
    put_u32(raw, value);
    return buf_add(out, raw, sizeof raw);
}

static int add_blob(buf *out, const char *text) {
    size_t len = text == NULL ? 0 : strlen(text);
    if (len > 0xFFFFFFFFu) {
        return -1;
    }
    if (add_u32(out, (uint32_t)len) != 0) {
        return -1;
    }
    return buf_add(out, text, len);
}

int aos_encode_request(buf *out, int argc, const char *const *argv,
                       const char *working_directory) {
    if (argc < 0) {
        return -1;
    }
    unsigned char version[2];
    version[0] = (unsigned char)(AOS_PROTOCOL_VERSION >> 8);
    version[1] = (unsigned char)AOS_PROTOCOL_VERSION;
    if (buf_add(out, version, sizeof version) != 0) {
        return -1;
    }
    if (add_u32(out, (uint32_t)argc) != 0) {
        return -1;
    }
    for (int i = 0; i < argc; ++i) {
        if (add_blob(out, argv[i]) != 0) {
            return -1;
        }
    }
    return add_blob(out, working_directory);
}

/* 一個往前走的讀取游標。每次取值前都檢查剩餘長度，
 * 這樣壞掉的 payload 只會讓 decode 失敗，不會讀出界。 */
typedef struct {
    const unsigned char *at;
    size_t left;
} cursor;

static int take(cursor *c, size_t n, const unsigned char **out) {
    if (c->left < n) {
        return -1;
    }
    *out = c->at;
    c->at += n;
    c->left -= n;
    return 0;
}

static int take_u32(cursor *c, uint32_t *out) {
    const unsigned char *raw;
    if (take(c, 4, &raw) != 0) {
        return -1;
    }
    *out = get_u32(raw);
    return 0;
}

/* 取一段長度前綴的位元組，複製成 NUL 結尾的字串。 */
static char *take_string(cursor *c) {
    uint32_t len;
    if (take_u32(c, &len) != 0) {
        return NULL;
    }
    const unsigned char *raw;
    if (take(c, len, &raw) != 0) {
        return NULL;
    }
    char *copy = malloc((size_t)len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, raw, len);
    copy[len] = '\0';
    return copy;
}

static int fail(char *reason, size_t cap, const char *message) {
    if (reason != NULL && cap > 0) {
        snprintf(reason, cap, "%s", message);
    }
    return -1;
}

int aos_decode_request(const void *payload, size_t len, aos_request *out,
                       char *reason, size_t reason_cap) {
    memset(out, 0, sizeof *out);
    cursor c = {(const unsigned char *)payload, len};

    const unsigned char *version_raw;
    if (take(&c, 2, &version_raw) != 0) {
        return fail(reason, reason_cap, "request_start 太短，連版本都放不下");
    }
    unsigned version = ((unsigned)version_raw[0] << 8) | version_raw[1];
    if (version != AOS_PROTOCOL_VERSION) {
        char message[128];
        snprintf(message, sizeof message,
                 "協定版本是 %u，這支只認得 %u（請兩邊一起更新）", version,
                 AOS_PROTOCOL_VERSION);
        return fail(reason, reason_cap, message);
    }

    uint32_t argc;
    if (take_u32(&c, &argc) != 0) {
        return fail(reason, reason_cap, "request_start 缺了 argc");
    }
    /* 上限擋的是「壞掉的長度欄位讓我們配置 40 億個指標」。 */
    if (argc > 65536u) {
        return fail(reason, reason_cap, "argc 大得不合理");
    }

    /* 迴圈放在 if 裡面而不是外面：argc 為 0 時 argv 是 NULL，
     * 「迴圈不會跑」和「指標是 NULL」之間的關聯要寫得看得出來——
     * 分開寫的話讀的人（和靜態分析器）都得自己去推。 */
    if (argc > 0) {
        out->argv = calloc(argc, sizeof *out->argv);
        if (out->argv == NULL) {
            return fail(reason, reason_cap, "記憶體不足");
        }
        for (uint32_t i = 0; i < argc; ++i) {
            out->argv[i] = take_string(&c);
            if (out->argv[i] == NULL) {
                /* 先把 argc 設成已經填好的數量，free 才知道要放幾個。 */
                out->argc = (int)i;
                aos_request_free(out);
                return fail(reason, reason_cap, "request_start 的參數壞掉了");
            }
        }
        out->argc = (int)argc;
    }

    out->working_directory = take_string(&c);
    if (out->working_directory == NULL) {
        aos_request_free(out);
        return fail(reason, reason_cap, "request_start 缺了工作目錄");
    }
    if (c.left != 0) {
        aos_request_free(out);
        return fail(reason, reason_cap, "request_start 後面有多餘的位元組");
    }
    return 0;
}

void aos_encode_exit(unsigned char *out4, int32_t code) {
    put_u32(out4, (uint32_t)code);
}

int aos_decode_exit(const void *payload, size_t len, int32_t *out) {
    if (len != 4) {
        return -1;
    }
    *out = (int32_t)get_u32((const unsigned char *)payload);
    return 0;
}
