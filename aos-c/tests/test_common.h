#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "aos/inst.h"
#include "test_check.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * 兩個輔助函式都是 `static inline`，因此即使某個編譯單元引入此標頭後沒有
 * 呼叫其中一個函式，也不會產生重複定義錯誤或未使用函式的警告。
 */

/*
 * 回傳含有 text 的可讀串流，並將位置設在第一個位元組。
 * tmpfile() 讓讀取器測試不必操作檔案系統；此串流和其他串流一樣使用
 * fclose() 關閉。
 */
static inline FILE *stream_from(const char *text)
{
    size_t length = strlen(text);
    FILE *stream = tmpfile();

    CHECK(stream != NULL);
    if (length > 0U) {
        CHECK(fwrite(text, 1U, length, stream) == length);
    }
    rewind(stream);
    return stream;
}

/*
 * 以指定的 argv 填入 inst，其他七個欄位則填入空字串，讓寫入測試只需覆寫
 * 要測試的內容。結果會借用每個字串而不擁有任何內容，這正是
 * aos_inst_write 預期接受的手動建構情況。
 */
static inline void fill_inst(aos_inst_t *inst, const char **argv, size_t argc)
{
    aos_inst_init(inst);
    inst->argc = argc;
    inst->argv = argv;
    inst->stdin_path = "";
    inst->stdout_path = "";
    inst->stderr_path = "";
    inst->exit_path = "";
    inst->cwd = "";
    inst->env_path = "";
    inst->extra = "";
}

/* 每個測試檔案都會匯出一個執行函式，回傳其執行的案例數量。 */

/* aos_inst_read 成功路徑：欄位、argv 分割、CRLF、重複使用。 */
size_t run_inst_read_tests(void);

/* aos_inst_read 拒絕路徑：EOF、INCOMPLETE、EMPTY_ARGV、讀取錯誤。 */
size_t run_inst_read_error_tests(void);

/* 記錄大小上限與 argv 數量上限，以及各自的邊界值前後。 */
size_t run_inst_limit_tests(void);

/* aos_inst_write 成功路徑，包括寫入／讀取的往返測試。 */
size_t run_inst_write_tests(void);

/* aos_inst_write 的驗證拒絕路徑，並確認不會輸出任何內容。 */
size_t run_inst_write_error_tests(void);

#endif
