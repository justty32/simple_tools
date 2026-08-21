#ifndef AOS_DAEMON_H
#define AOS_DAEMON_H
/* daemon.h — 常駐端。
 *
 * ## 併發模型：一條連線一條執行緒，全部阻塞
 *
 * 這是整個專案最重要的一個決定，所以講清楚為什麼。
 *
 * C++ 版是單執行緒 + 協程（asio）。換到 C99 沒有協程，剩下兩條路：
 *
 *   (a) epoll + 狀態機：每個命令都要拆成「讀到一半」「寫到一半」的狀態，
 *       連 echo 這種三行的東西都會變成一個 switch。而且外掛作者也得這樣寫。
 *   (b) 一條連線一條執行緒 + 阻塞 IO：命令是平鋪直敘的函式，想擋多久擋多久。
 *
 * 選 (b)。這個 daemon 的併發量是「同時幾個終端機」，不是幾萬條連線，
 * 一條執行緒 8 MiB 的虛擬位址空間完全不是問題。換來的是：
 *
 *   - 命令可以直接 while (read) { write }，不必拆狀態機
 *   - 外掛可以直接呼叫阻塞的第三方函式庫（curl_easy_perform 之類）
 *   - C++ 版為了不卡住 io_context 而寫的那整套 worker thread + channel 接縫，
 *     在這裡根本不需要存在
 *
 * 代價是任何跨連線的共用狀態都要鎖（見 runtime.h）。這個代價是明確的、
 * 局部的，而且只在你真的加共用狀態時才付。
 *
 * ## 收工
 *
 * 「收工」= 關掉 listen fd（不再接受新連線）+ 叫醒所有背景工作。
 * 已經在跑的連線會做完自己的事。不是硬砍。
 */

#include "runtime.h"
#include "socket_path.h"

/* 跑到收工為止。回傳給 main 用的 exit code。 */
int aos_run_daemon(const char *path);

/* 處理一條已經接受的連線，直到它結束。connection.c 實作。 */
void aos_serve_connection(int fd, aos_runtime *runtime);

#endif /* AOS_DAEMON_H */
