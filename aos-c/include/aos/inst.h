#ifndef AOS_INST_H
#define AOS_INST_H

#include <stddef.h>
#include <stdio.h>

/*
 * 一個指令：序列化的行程啟動資訊，以及在串流與記憶體之間搬移該資訊的
 * 讀取器與寫入器。
 *
 * 每個指令包含八行，每個欄位各占一行：
 *   argv、stdin、stdout、stderr、exit、cwd、env、extra。
 * argv 行使用定位字元分隔，不使用引號或跳脫；空白、引號與反斜線都是一般
 * 字元，而相鄰的定位字元會保留空引數。包括第八行在內，每一行都必須以
 * 換行字元結尾，藉此區分截斷的記錄，以及第八行剛好為空的完整記錄。
 * 輸入可使用 LF 或 CRLF；寫入器一律輸出 LF。
 *
 * 讀取以串流為基礎：aos_inst_read 接受 FILE * 並且只取用一筆記錄，因此可
 * 搭配管線與 stdin 使用；記憶體用量受最長記錄限制，而不是取決於輸入大小。
 * 其取捨是無法隨機存取檔案中的每筆記錄。
 */

/*
 * 單一指令可攜帶的引數數量上限，不包含結尾的 NULL。讀取器與寫入器共用此
 * 上限，因此寫入器接受的每筆記錄都能由讀取器解析還原。若數值必須與編譯進
 * 函式庫的值一致，請呼叫 aos_inst_argv_max()，不要直接讀取此巨集。
 */
#define AOS_INST_ARGV_MAX 256U

/* 單筆序列化記錄的行數；修改此值也會改變格式。 */
#define AOS_INST_LINE_COUNT 8U

/*
 * aos_inst_read 預設的單筆記錄位元組上限。此值限制一筆記錄可占用的儲存
 * 空間，包含八行內容以及每行結尾的 NUL。這只是預設值，而非 API 的固定
 * 限制：任何正值都可傳給 aos_inst_read_max。
 */
#define AOS_INST_RECORD_MAX_BYTES (1024U * 1024U)

/*
 * 讀取一個指令，或驗證並寫入一個指令的結果。
 *
 * 使用端會依據這些數值進行編譯，因此新狀態只能附加在後；重新排序會在沒有
 * 警告的情況下改變既有代碼的意義。
 */
typedef enum aos_inst_state {
    AOS_INST_OK = 0,
    AOS_INST_INVALID_ARGUMENT,
    /* 讀取：串流結束前沒有任何記錄開始。 */
    AOS_INST_EOF,
    /* 讀取：串流在記錄中途結束。 */
    AOS_INST_INCOMPLETE,
    /* 讀取：記錄超過呼叫端指定的位元組上限。 */
    AOS_INST_TOO_LONG,
    /* 讀取：串流回報錯誤。 */
    AOS_INST_READ_ERROR,
    /* 讀取或寫入：argv 行不含任何引數。 */
    AOS_INST_EMPTY_ARGV,
    /* 讀取或寫入：argv 行包含超過 AOS_INST_ARGV_MAX 個引數。 */
    AOS_INST_TOO_MANY_ARGS,
    AOS_INST_ALLOC_FAILED,
    /* 寫入：某個 argv 項目或欄位指標為 NULL。 */
    AOS_INST_NULL_ARGUMENT,
    AOS_INST_NULL_FIELD,
    /* 寫入：某個值包含格式用作分隔符號的位元組。 */
    AOS_INST_ARGUMENT_CONTAINS_TAB,
    AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK,
    AOS_INST_FIELD_CONTAINS_LINE_BREAK,
    /* 寫入：串流回報錯誤，錯誤可能發生在記錄中途。 */
    AOS_INST_WRITE_ERROR
} aos_inst_state;

/*
 * 一個指令。
 *
 * aos_inst_read 成功後，每個字串都會指向此結構所擁有的儲存空間，並持續
 * 有效，直到下一次讀取或呼叫 aos_inst_free。呼叫端也可以手動填入公開欄位，
 * 將借用的記錄交給 aos_inst_write；此時私有欄位會維持為零，
 * aos_inst_free 不會釋放任何內容，因此借用的字串不會受到影響。
 */
typedef struct aos_inst {
    size_t argc;
    /* 成功讀取後，argv[argc] 一律為 NULL。 */
    const char **argv;
    /* 第 2 至第 6 行是路徑；空白行會保留為空字串。 */
    const char *stdin_path;
    const char *stdout_path;
    const char *stderr_path;
    const char *exit_path;
    const char *cwd;
    /* 解析器不解讀第 7 與第 8 行的內容，且兩者都可以為空。 */
    const char *env_path;
    const char *extra;

    /*
     * 供讀取器私用。storage 為上述每個字串提供儲存空間；argv_slots 則支援
     * argv，而 argv 只會是其中一者的檢視。這些容量資訊讓同一個指令連續讀取
     * 時得以重複使用兩塊已配置的記憶體。請將這四個欄位視為不透明資料：只有
     * aos_inst_read 與 aos_inst_free 可以修改它們。
     */
    char *storage;
    size_t storage_capacity;
    const char **argv_slots;
    size_t argv_slots_capacity;
} aos_inst_t;

/*
 * 將指令歸零。第一次呼叫 aos_inst_read 前必須執行；在指定公開欄位之前，
 * 也可用此函式為 aos_inst_write 準備指令。
 */
void aos_inst_init(aos_inst_t *inst);

/*
 * 釋放指令擁有的所有內容，並將其恢復為初始化狀態。傳入 NULL、已釋放的指令，
 * 或手動指定欄位且不擁有任何內容的指令，皆可安全呼叫；最後一種情況不會
 * 釋放任何內容。
 */
void aos_inst_free(aos_inst_t *inst);

/*
 * 從 stream 將下一筆記錄讀入 inst，位元組上限為
 * AOS_INST_RECORD_MAX_BYTES。
 *
 * inst 必須已初始化；此函式首先會捨棄其中既有的記錄。若串流在兩筆記錄之間
 * 正常結束，則回傳 AOS_INST_EOF，並讓 inst 保持為空。任何失敗也都會讓 inst
 * 保持為空，而到目前為止取用的位元組將無法取回：串流不能倒轉，因此
 * AOS_INST_INCOMPLETE 與 AOS_INST_TOO_LONG 會終止本次處理，而不是讓呼叫端
 * 重試。
 */
aos_inst_state aos_inst_read(FILE *stream, aos_inst_t *inst);

/*
 * 使用明確指定且必須為正值的單筆記錄位元組上限來讀取下一筆記錄。若記錄所需
 * 的儲存空間超過 max_record_bytes，則回傳 AOS_INST_TOO_LONG，而不會無限制地
 * 配置記憶體；指令檔案是信任邊界，因此一律套用上限。
 */
aos_inst_state aos_inst_read_max(FILE *stream, aos_inst_t *inst,
                                 size_t max_record_bytes);

/*
 * 將一個指令以八行寫入 stream，每行都以 LF 結尾；重複呼叫會持續附加可供
 * aos_inst_read 取用的記錄。
 *
 * 寫入第一個位元組之前會先驗證整筆記錄，因此遭拒絕的指令不會改動串流。
 * 若寫入在中途失敗，仍可能已輸出部分記錄。stream 與 instruction 的所有權
 * 都由呼叫端保留。
 */
aos_inst_state aos_inst_write(FILE *stream, const aos_inst_t *inst);

/* 編譯進此函式庫的 argv 數量上限。 */
size_t aos_inst_argv_max(void);

/* 回傳 state 的靜態、可讀文字說明。 */
const char *aos_inst_state_string(aos_inst_state state);

#endif
