#ifndef AOS_MODULES_H
#define AOS_MODULES_H
/* modules.h — **編進 aos-core 裡**的擴充模組。
 *
 * 跟外掛（plugin.h）的差別只有「怎麼載入」，**契約完全一樣**：
 * 同一個 aos_plugin 結構、同一張 aos_command 表、同一個 aos_host。
 * 所以你可以先編進來開發，之後想改成 .so 只要換 build 的方式，程式碼不用動。
 *
 *   編進來（modules.c）              外掛（plugin.c）
 *   ─────────────────                ─────────────
 *   編譯期就決定                      啟動時 dlopen
 *   改了要重編 aos-core               丟一個 .so 就好
 *   ABI 不會錯配（一起編）             ABI 要對版本
 *   可以跨邊界做 LTO                   不行
 *
 * ## 適合編進來的情況
 *
 * - 用 C++ 寫的子專案，跟 aos-core 一起開發、一起改
 * - 還在動介面的東西（編進來就不必每次同步 ABI 版本）
 * - 不想處理 .so 部署的東西
 *
 * ## 怎麼加一個
 *
 * 1. 寫一個 aos_plugin_init 形狀的函式（C 或 C++ 都行）
 * 2. 在 modules.c 的表裡加一行
 * 3. 把它的原始檔加進 Makefile 的 MODULE_SRCS
 *
 * C++ 的話**一定要看 include/aos/plugin.hpp**：例外不能穿過 C 邊界，
 * 那裡有一個包好的 guard 讓你想忘也忘不掉。 */

#include "../include/aos/plugin.h"

/* 跟外掛的進入點同型。 */
typedef const aos_plugin *(*aos_module_init_fn)(const aos_host *host);

/* 把所有編進來的模組接進命令樹。daemon 啟動時、載外掛**之前**呼叫，
 * 這樣撞名時編進來的贏（它跟 aos-core 一起編，比較不會是意外）。 */
void aos_modules_load(void);

/* 呼叫每個模組的 shutdown。不 dlclose，因為它們本來就在同一個執行檔裡。 */
void aos_modules_unload(void);

/* 給 `aos daemon plugins` 用：編進來的也要列出來，不然使用者看不到它們。 */
size_t aos_module_count(void);
const char *aos_module_name(size_t index);

#endif /* AOS_MODULES_H */
