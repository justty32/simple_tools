# C API

公開的 C ABI 由 `<aos/aos.h>` 宣告。這個標頭相容 C99，不含任何 C++ 型別，並
對外提供一個不透明的 `aos_instruction` handle。

## 建置與連結

CMake 會產生供連結期使用的 `libaos.so`、SONAME `libaos.so.0`，以及帶版號的
函式庫 `libaos.so.0.1.0`。從儲存庫根目錄用以下指令編譯用戶端程式：

```sh
cc -std=c99 example.c -Iinclude -Lbuild -Wl,-rpath,"$PWD/build" -laos
```

`-laos` 在連結時會挑選 `libaos.so`；動態載入器則會記錄並載入它的 SONAME。
上面的 rpath 對於尚未安裝的建置很方便。已安裝或已封裝的用戶端則應改為把
`libaos.so.0` 放到平台正常的函式庫搜尋路徑上。

## 完整範例

```c
#include <aos/aos.h>

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    static const char record[] =
        "{\"argv\":[\"printf\",\"hello from C\\n\"],"
        "\"exit\":\"status.txt\"}";
    aos_instruction *inst = aos_instruction_new();
    aos_exec_result result;
    aos_inst_state istate;
    aos_exec_state estate;
    char *encoded = NULL;
    size_t needed = 0;
    int rc = 1;

    if (inst == NULL) {
        fputs("allocation failed\n", stderr);
        return 1;
    }

    istate = aos_instruction_read_buffer(record, sizeof record - 1, inst);
    if (istate != AOS_INST_OK) {
        fprintf(stderr, "parse: %s\n", aos_inst_state_string(istate));
        goto done;
    }

    estate = aos_instruction_execute(inst, &result);
    if (estate != AOS_EXEC_OK) {
        fprintf(stderr, "execute: %s (errno %d)\n",
                aos_exec_state_string(estate), result.error);
        goto done;
    }
    printf("child status=%d signalled=%d timed_out=%d\n",
           result.status, result.signalled, result.timed_out);

    istate = aos_instruction_write_buffer(inst, NULL, 0, &needed);
    if (istate != AOS_INST_BUFFER_TOO_SMALL) goto done;
    encoded = (char *)malloc(needed + 1);
    if (encoded == NULL) goto done;
    istate = aos_instruction_write_buffer(inst, encoded, needed + 1, &needed);
    if (istate != AOS_INST_OK) goto done;
    fputs(encoded, stdout);
    rc = 0;

done:
    free(encoded);
    aos_instruction_free(inst);
    return rc;
}
```

這段程式解析一筆指令、執行它、印出結果、把指令序列化，並釋放所有自己持有的
配置。

## 生命週期與欄位

`aos_instruction_new()` 會建立一筆預設指令，並在配置失敗時回傳 `NULL`。
`aos_instruction_clear()` 會還原成預設值，而 `aos_instruction_free()` 接受
`NULL`。

argv 請使用 `aos_instruction_push_arg()` 搭配 `aos_instruction_argc()` 與
`aos_instruction_arg()`。五個字串欄位透過欄位的 getter/setter 以
`AOS_FIELD_STDIN`、`STDOUT`、`STDERR`、`EXIT` 與 `CWD` 來選取。環境變數項目則
使用 count/key/value 的 getter 與 `aos_instruction_set_env()`；設定一個已存在
的 key 會取代它。key 依字典序排列，必須非空，且不能包含 `=`。逾時的
getter/setter 值以毫秒為單位；零會停用截止時間。

所有 setter 都會複製它們的字串。getter 回傳的字串是借用(borrowed)的、以 NUL
結尾，且只在該指令被變更、清除、讀入或釋放之前有效。無效的 handle、欄位與
索引會依回傳型別回傳其記載的無效狀態、零或 `NULL`。

## 讀取、寫入與執行

`aos_instruction_read_buffer()` 會從提供的位元組中剛好解析一個 JSON 值。
`aos_instruction_read_fd()` 會一路讀到 EOF、讓呼叫端持有的 fd 保持開啟、將其
標記為 close-on-exec、對被中斷的讀取進行重試，並把結果解析成一筆指令。兩者
都會在解析前清除目的地，並使用預設的記錄/總量上限。兩者都不接受多筆記錄的
JSON Lines 批次，也都不使用 `FILE *`。

序列化採用兩次呼叫的大小查詢，如範例所示。所需的位元組數包含最後的 LF，但
不含為方便而附加的 NUL。緩衝區必須能容納 `needed + 1` 個位元組。查詢或緩衝區
過小的呼叫會回傳 `AOS_INST_BUFFER_TOO_SMALL`、回報所需的數量，並讓緩衝區維持
原狀不動。

`aos_instruction_execute()` 會重置一個非 NULL 的 result、等待命令完成，並回傳
一個 `aos_exec_state`。result 的 `status` 是子行程的離開值或 `128 + signal`；
`signalled` 標示因訊號而終止，`timed_out` 標示由函式庫發起的逾時，而 `error`
則為適用的 API 失敗攜帶 `errno`。子行程若回傳如 1、126 或 127 這類狀態，仍會
回傳 `AOS_EXEC_OK`。完整規則請見[execution semantics](exec.md)。

回傳狀態字串的函式會回傳靜態的診斷字串。`aos_inst_*_max()` 系列函式會揭露編譯
期的上限，而 `aos_version_string()` 會回報函式庫版本。C++ 例外絕不會跨越 C
邊界：回傳狀態的操作會把它們對應成配置失敗；其他回傳形式則使用 `NULL` 或零，
而 void 的清理操作則會抑制它們。

## 執行緒

這套 API 可以安全地在多執行緒行程中使用。具體來說，執行會在 `fork` 之前，於
父行程中完整地合併環境、配置 `argv`/`envp` 並解析 PATH。子行程接著在 `execve`
之前只使用 async-signal-safe 的 POSIX 操作，藉此避開其他執行緒留下的配置器
鎖。

不同的 handle 可以並行使用。單一 handle 沒有內部鎖：請勿並行變動它、在另一個
執行緒正變動它時執行它，或在變動之後仍保留一個借用的 getter 指標。與其他
行程層級(process-wide)的 API 一樣，呼叫端也必須同步對行程環境的並行變更。

## ABI 穩定性

SONAME 就是 ABI 的邊界。相容的釋出版本會保留 `libaos.so.0`；不相容的變更則
需要一個新的 SOVERSION。只要保留該 SONAME，既有的已匯出函式簽章、公開的結構
佈局，以及既有的列舉數值都不會改變。列舉值是凍結的，新值只能被附加，絕不會
透過重新編號插入，也不會被重複使用。

新的函式與被附加的列舉值是可以新增的。不透明的 `aos_instruction` 內部結構、
診斷文字、實作細節、版本號，以及查詢到的資源上限，都可以在不破壞 C ABI 的
情況下改變。編譯期的 `AOS_VERSION_*` 巨集描述的是標頭；載入的函式庫請改用
`aos_version_string()`。
