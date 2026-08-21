#ifndef AOS_PROTOCOL_H
#define AOS_PROTOCOL_H
/* protocol.h — 線上格式。純函式，不碰 socket，所以測得起來。
 *
 * ## 訊框
 *
 *     [u32 大端 payload 長度][u8 種類][payload]
 *
 * 長度**不含**種類那個位元組，所以長度 0 是合法的——stdin_end 就是一個沒有
 * payload 的訊框。（把種類算進長度的話 0 會變成不合法，那是很容易踩到的邊界。）
 *
 * ## 控制訊息不是 JSON
 *
 * C++ 版用 JSON 帶 argv 和工作目錄。這一版改成長度前綴的二進位：
 *
 *     request_start:  [u16 協定版本][u32 argc][u32 len + bytes]... [u32 len + cwd]
 *     exit:           [i32 大端 exit code]
 *
 * 兩個理由。一是**二進位安全是免費的**：JSON 要處理跳脫，而參數裡可以有任何位元組；
 * 長度前綴不必跳脫，寫錯的機會少一整類。二是**不必為了兩個小訊息拉一個 JSON 函式庫**，
 * 這一層因此零相依。
 *
 * 代價是人不能直接用眼睛讀線上內容。想看收到什麼就用 `aos describe`，它會把
 * argv、工作目錄、stdin 位元組數原樣印出來。
 */

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

/* 這一版的協定。兩端不合就拒絕，不猜。 */
#define AOS_PROTOCOL_VERSION 2u

#define AOS_FRAME_HEADER_SIZE 5u
/* 單一訊框的 payload 上限。stdin 總量沒有上限（是串流的），這只限制一塊。
 *
 * 這兩個都寫成 size_t 而不是 unsigned：它們一律拿來跟長度比大小，
 * 用 unsigned 的話乘法會在 32 位元裡算完才放大，數字再大一點就會安靜地繞回去。
 * 目前的值不會，但這種常數遲早有人改大。 */
#define AOS_MAX_PAYLOAD ((size_t)8 * 1024 * 1024)
/* 寫出去時自動切塊的大小。 */
#define AOS_CHUNK_SIZE ((size_t)64 * 1024)

typedef enum {
    AOS_FRAME_REQUEST_START = 1,
    AOS_FRAME_STDIN_CHUNK = 2,
    AOS_FRAME_STDIN_END = 3,
    AOS_FRAME_STDOUT_CHUNK = 4,
    AOS_FRAME_STDERR_CHUNK = 5,
    AOS_FRAME_EXIT = 6
} aos_frame_kind;

int aos_frame_kind_known(unsigned kind);

/* 訊框標頭的編解碼。header 必須指向 AOS_FRAME_HEADER_SIZE 個位元組。 */
void aos_encode_header(unsigned char *header, uint32_t payload_len,
                       unsigned kind);
void aos_decode_header(const unsigned char *header, uint32_t *payload_len,
                       unsigned *kind);

/* 一次呼叫要送的東西。decode 之後 argv 與 cwd 都是這個結構自己配置的，
 * 用完要 aos_request_free。 */
typedef struct {
    int argc;
    char **argv;         /* 每個都是 NUL 結尾（命令列參數本來就不能含 \0）*/
    char *working_directory;
} aos_request;

void aos_request_free(aos_request *request);

/* 組出 request_start 的 payload。失敗回 -1。 */
int aos_encode_request(buf *out, int argc, const char *const *argv,
                       const char *working_directory);

/* 拆 request_start 的 payload。成功回 0；格式不對回 -1 並把原因寫進
 * reason（NUL 結尾，最多 reason_cap - 1 個字元）。 */
int aos_decode_request(const void *payload, size_t len, aos_request *out,
                       char *reason, size_t reason_cap);

void aos_encode_exit(unsigned char *out4, int32_t code);
int aos_decode_exit(const void *payload, size_t len, int32_t *out);

#endif /* AOS_PROTOCOL_H */
