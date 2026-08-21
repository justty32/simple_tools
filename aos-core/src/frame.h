#ifndef AOS_FRAME_H
#define AOS_FRAME_H
/* frame.h — 在一個 fd 上讀／寫一個訊框。全部阻塞。
 *
 * 「阻塞」在這裡不是偷懶，是整個架構的前提：daemon 一條連線一條執行緒、
 * CLI 一個方向一條執行緒，所以每條執行緒都可以理直氣壯地擋在 read() 上。
 * 換來的是所有命令都能寫成平鋪直敘的程式，不必拆成狀態機。
 *
 * 回傳碼一律：0 成功、-1 出錯（含對方關閉連線）。 */

#include <stddef.h>
#include <stdint.h>

#include "buf.h"
#include "protocol.h"

/* 一定要讀滿 n 個位元組才回來。中途 EOF 算失敗。 */
int aos_read_exact(int fd, void *out, size_t n);

/* 一定要全部寫完才回來。短寫會自己續寫。 */
int aos_write_all(int fd, const void *data, size_t n);

/* 讀一個訊框。payload 會被 append 到 out（呼叫端要自己先清空）。
 * 超過 AOS_MAX_PAYLOAD 就當成協定錯誤，不配置。 */
int aos_read_frame(int fd, unsigned *kind, buf *out);

/* 寫一個訊框。len 可以是 0（stdin_end 就是這樣）。 */
int aos_write_frame(int fd, unsigned kind, const void *data, size_t len);

/* 寫一串資料，超過 AOS_CHUNK_SIZE 會自動切成多個訊框。
 * len 為 0 時**不送任何東西**——空的寫入沒有意義，送出去只會讓對方多醒一次。 */
int aos_write_stream(int fd, unsigned kind, const void *data, size_t len);

#endif /* AOS_FRAME_H */
