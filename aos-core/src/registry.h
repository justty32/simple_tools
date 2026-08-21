#ifndef AOS_REGISTRY_H
#define AOS_REGISTRY_H
/* registry.h — 命令樹：有哪些命令、怎麼沿著名字往下走、怎麼派發。
 *
 * ## 新增一個內建命令（三步驟）
 *
 *   1. src/commands/ 加一個 .c，寫一個
 *      int f(aos_session *s, int argc, const char *const *argv, void *user)
 *   2. src/commands/builtin.h 加一行宣告
 *   3. registry.c 的表加一列
 *
 * CMake 是用 glob 掃 src/，新檔案不必手動加進建置。
 * 子命令就是在 registry.c 裡多開一張子表，掛到分組節點的 children。
 *
 * ## 外掛的命令
 *
 * 走完全一樣的 aos_command 結構（見 include/aos/plugin.h）。daemon 啟動時
 * 把外掛的表和內建的表接成同一棵樹，所以派發這一層看不出誰是誰。 */

#include <stddef.h>

#include "../include/aos/plugin.h"
#include "buf.h"
#include "runtime.h"
#include "session.h"

/* 根表。外掛載入後會變（見 aos_registry_install_plugins）。 */
const aos_command *aos_commands(size_t *count);

/* 沿著命令樹往下走的結果。 */
typedef struct {
    const aos_command *command; /* 沒對上任何名字就是 NULL */
    int depth;                  /* 命令路徑吃掉了幾個參數 */
} aos_resolution;

aos_resolution aos_resolve_command(int argc, const char *const *argv);

/* help 與「這是個分組」的提示共用同一份渲染。寫進 out，不直接輸出——
 * 因為 `aos help` 是正常輸出（stdout），而「你沒給命令」是錯誤（stderr）。 */
int aos_render_command_list(buf *out, const char *const *path, int path_len,
                            const aos_command *level, size_t level_count);

/* 所有命令的唯一入口。回傳 exit status。 */
int aos_handle_command(aos_session *session, aos_runtime *runtime, int argc,
                       const char *const *argv);

/* 把一張命令表接進根表。可以呼叫很多次——**編進來的模組**（modules.c）和
 * **dlopen 進來的外掛**（plugin.c）走的是同一個函式，先來的先贏。
 *
 * source 只用在錯誤訊息裡（"hello" 或 "編進來的 greet"），方便撞名時找人。
 * 撞名的那個命令不載入，並在 daemon 的 stderr 記一行。 */
void aos_registry_add(const aos_command *table, size_t count,
                      const char *source);

void aos_registry_reset(void);

#endif /* AOS_REGISTRY_H */
