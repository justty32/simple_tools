#include "aos/inst.h"

#include <stdlib.h>
#include <string.h>

/* 依序列化順序排列的七個非 argv 欄位。 */
#define AOS_INST_FIELD_COUNT 7U

/* 儲存空間的初始配置量；讀取時會擴充至足以容納最長的記錄。 */
#define AOS_INST_STORAGE_MIN 256U

void aos_inst_init(aos_inst_t *inst)
{
    if (inst != NULL) {
        memset(inst, 0, sizeof(*inst));
    }
}

void aos_inst_free(aos_inst_t *inst)
{
    if (inst == NULL) {
        return;
    }
    /*
     * 若指令的欄位是手動指定，這兩者都會是 NULL，
     * 因此傳給 aos_inst_write 的借用記錄絕不會在此處被釋放。
     */
    free(inst->storage);
    free(inst->argv_slots);
    memset(inst, 0, sizeof(*inst));
}

size_t aos_inst_argv_max(void)
{
    return AOS_INST_ARGV_MAX;
}

/*
 * 清除上一次讀取留下的記錄，但保留其兩塊已配置的記憶體，讓呼叫端以同一個
 * 指令讀取整個串流時，緩衝區達到最長記錄所需的大小後便不再重新配置。
 * 所有公開欄位都會在此清空，並且只在完整讀取記錄後才設定，因此每次失敗時
 * inst 都會保持為空。
 */
static void inst_clear(aos_inst_t *inst)
{
    inst->argc = 0U;
    inst->argv = NULL;
    inst->stdin_path = NULL;
    inst->stdout_path = NULL;
    inst->stderr_path = NULL;
    inst->exit_path = NULL;
    inst->cwd = NULL;
    inst->env_path = NULL;
    inst->extra = NULL;
}

/* 在不超過上限的前提下，確保儲存空間可容納 `needed` 個位元組。 */
static aos_inst_state storage_reserve(aos_inst_t *inst, size_t needed,
                                      size_t max_record_bytes)
{
    size_t capacity;
    char *grown;

    if (needed > max_record_bytes) {
        return AOS_INST_TOO_LONG;
    }
    if (needed <= inst->storage_capacity) {
        return AOS_INST_OK;
    }

    capacity = (inst->storage_capacity == 0U) ? AOS_INST_STORAGE_MIN
                                              : inst->storage_capacity;
    while (capacity < needed) {
        /* 將容量限制在上限內，也可避免倍增時發生溢位。 */
        if (capacity > max_record_bytes / 2U) {
            capacity = max_record_bytes;
            break;
        }
        capacity *= 2U;
    }
    if (capacity > max_record_bytes) {
        capacity = max_record_bytes;
    }

    grown = (char *)realloc(inst->storage, capacity);
    if (grown == NULL) {
        return AOS_INST_ALLOC_FAILED;
    }
    inst->storage = grown;
    inst->storage_capacity = capacity;
    return AOS_INST_OK;
}

/*
 * 從 *length 指定的位置開始，將一個以 NUL 結尾的行附加至儲存空間，
 * 並讓 *length 最後指向該終止字元之後。
 *
 * 若該行以換行字元結尾則回傳 AOS_INST_OK；若串流在該行第一個位元組之前
 * 結束則回傳 AOS_INST_EOF；若已讀到位元組卻沒有換行字元則回傳
 * AOS_INST_INCOMPLETE；其他情況則回傳對應的失敗狀態。
 */
static aos_inst_state read_line(FILE *stream, aos_inst_t *inst,
                                size_t *length, size_t max_record_bytes)
{
    size_t start = *length;
    size_t used = *length;
    aos_inst_state state;
    int c;

    for (;;) {
        c = getc(stream);
        if (c == EOF || c == '\n') {
            break;
        }
        state = storage_reserve(inst, used + 1U, max_record_bytes);
        if (state != AOS_INST_OK) {
            return state;
        }
        inst->storage[used++] = (char)c;
    }

    if (c == EOF) {
        if (ferror(stream)) {
            return AOS_INST_READ_ERROR;
        }
        if (used == start) {
            return AOS_INST_EOF;
        }
    }

    /* 移除 CRLF 組合中的 CR；單獨位於結尾的 CR 則視為資料。 */
    if (c == '\n' && used > start && inst->storage[used - 1U] == '\r') {
        --used;
    }

    state = storage_reserve(inst, used + 1U, max_record_bytes);
    if (state != AOS_INST_OK) {
        return state;
    }
    inst->storage[used++] = '\0';
    *length = used;

    return (c == '\n') ? AOS_INST_OK : AOS_INST_INCOMPLETE;
}

/*
 * 直接在原處依定位字元分割 argv 行，並讓 argv 指向分割結果。
 * 兩項限制都會在寫入第一個 NUL 前檢查，因此遭拒絕的行不會停留在只分割一半
 * 的狀態。
 */
static aos_inst_state split_argv(aos_inst_t *inst, char *line)
{
    size_t argc = 1U;
    size_t index;
    char *cursor;

    if (*line == '\0') {
        return AOS_INST_EMPTY_ARGV;
    }

    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '\t') {
            ++argc;
        }
    }
    if (argc > AOS_INST_ARGV_MAX) {
        return AOS_INST_TOO_MANY_ARGS;
    }

    if (argc + 1U > inst->argv_slots_capacity) {
        const char **grown = (const char **)realloc(
            inst->argv_slots, (argc + 1U) * sizeof(*grown));

        if (grown == NULL) {
            return AOS_INST_ALLOC_FAILED;
        }
        inst->argv_slots = grown;
        inst->argv_slots_capacity = argc + 1U;
    }

    index = 0U;
    inst->argv_slots[index++] = line;
    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '\t') {
            *cursor = '\0';
            inst->argv_slots[index++] = cursor + 1;
        }
    }
    inst->argv_slots[argc] = NULL;

    inst->argv = inst->argv_slots;
    inst->argc = argc;
    return AOS_INST_OK;
}

aos_inst_state aos_inst_read_max(FILE *stream, aos_inst_t *inst,
                                 size_t max_record_bytes)
{
    /*
     * 此處使用位移量而非指標：讀取記錄期間，storage 可能因 realloc 而移動，
     * 所以只能在最後一行讀入後，才把八個欄位的位置轉換成指標。
     * 不必預先測量記錄大小的全部代價，就是堆疊上的八個 size_t。
     */
    size_t offsets[AOS_INST_LINE_COUNT];
    size_t length = 0U;
    size_t index;
    aos_inst_state state;

    if (stream == NULL || inst == NULL || max_record_bytes == 0U) {
        return AOS_INST_INVALID_ARGUMENT;
    }

    inst_clear(inst);

    for (index = 0U; index < AOS_INST_LINE_COUNT; ++index) {
        offsets[index] = length;
        state = read_line(stream, inst, &length, max_record_bytes);
        if (state == AOS_INST_EOF) {
            /* 若位於記錄之間，這是正常結束；若位於記錄內部則不是。 */
            return (index == 0U) ? AOS_INST_EOF : AOS_INST_INCOMPLETE;
        }
        if (state != AOS_INST_OK) {
            return state;
        }
    }

    state = split_argv(inst, inst->storage + offsets[0]);
    if (state != AOS_INST_OK) {
        return state;
    }

    inst->stdin_path = inst->storage + offsets[1];
    inst->stdout_path = inst->storage + offsets[2];
    inst->stderr_path = inst->storage + offsets[3];
    inst->exit_path = inst->storage + offsets[4];
    inst->cwd = inst->storage + offsets[5];
    inst->env_path = inst->storage + offsets[6];
    inst->extra = inst->storage + offsets[7];
    return AOS_INST_OK;
}

aos_inst_state aos_inst_read(FILE *stream, aos_inst_t *inst)
{
    return aos_inst_read_max(stream, inst, AOS_INST_RECORD_MAX_BYTES);
}

/* CR 也會被拒絕，因為結尾的 CR 會被當成 CRLF 的一部分而消耗掉。 */
static int contains_line_break(const char *text)
{
    return strchr(text, '\n') != NULL || strchr(text, '\r') != NULL;
}

/* 依序列化順序排列的七個非 argv 欄位。 */
static void inst_fields(const aos_inst_t *inst,
                        const char *fields[AOS_INST_FIELD_COUNT])
{
    fields[0] = inst->stdin_path;
    fields[1] = inst->stdout_path;
    fields[2] = inst->stderr_path;
    fields[3] = inst->exit_path;
    fields[4] = inst->cwd;
    fields[5] = inst->env_path;
    fields[6] = inst->extra;
}

/* 在任何內容寫入串流之前，先驗證整筆記錄。 */
static aos_inst_state inst_validate(const aos_inst_t *inst)
{
    const char *fields[AOS_INST_FIELD_COUNT];
    size_t index;

    if (inst == NULL) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    if (inst->argc == 0U) {
        return AOS_INST_EMPTY_ARGV;
    }
    if (inst->argv == NULL) {
        return AOS_INST_NULL_ARGUMENT;
    }
    if (inst->argc > AOS_INST_ARGV_MAX) {
        return AOS_INST_TOO_MANY_ARGS;
    }

    for (index = 0U; index < inst->argc; ++index) {
        const char *argument = inst->argv[index];

        if (argument == NULL) {
            return AOS_INST_NULL_ARGUMENT;
        }
        if (strchr(argument, '\t') != NULL) {
            return AOS_INST_ARGUMENT_CONTAINS_TAB;
        }
        if (contains_line_break(argument)) {
            return AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK;
        }
    }
    /*
     * 單一空引數沒有定位字元可標示其邊界，因此序列化後會成為空白的 argv 行，
     * 讀回時也會被視為完全沒有引數，而無法往返還原成 argc == 1。
     */
    if (inst->argc == 1U && inst->argv[0][0] == '\0') {
        return AOS_INST_EMPTY_ARGV;
    }

    inst_fields(inst, fields);
    for (index = 0U; index < AOS_INST_FIELD_COUNT; ++index) {
        if (fields[index] == NULL) {
            return AOS_INST_NULL_FIELD;
        }
        if (contains_line_break(fields[index])) {
            return AOS_INST_FIELD_CONTAINS_LINE_BREAK;
        }
    }

    return AOS_INST_OK;
}

aos_inst_state aos_inst_write(FILE *stream, const aos_inst_t *inst)
{
    const char *fields[AOS_INST_FIELD_COUNT];
    aos_inst_state state;
    size_t index;

    if (stream == NULL) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    state = inst_validate(inst);
    if (state != AOS_INST_OK) {
        return state;
    }

    /* 第 1 行：以定位字元分隔的 argv，接著是 LF。 */
    for (index = 0U; index < inst->argc; ++index) {
        if (index > 0U && fputc('\t', stream) == EOF) {
            return AOS_INST_WRITE_ERROR;
        }
        if (fputs(inst->argv[index], stream) == EOF) {
            return AOS_INST_WRITE_ERROR;
        }
    }
    if (fputc('\n', stream) == EOF) {
        return AOS_INST_WRITE_ERROR;
    }

    inst_fields(inst, fields);

    /* 第 2 至第 8 行：每行包含一個已驗證的欄位及一個 LF。 */
    for (index = 0U; index < AOS_INST_FIELD_COUNT; ++index) {
        if (fputs(fields[index], stream) == EOF ||
            fputc('\n', stream) == EOF) {
            return AOS_INST_WRITE_ERROR;
        }
    }

    return AOS_INST_OK;
}

const char *aos_inst_state_string(aos_inst_state state)
{
    switch (state) {
    case AOS_INST_OK:
        return "ok";
    case AOS_INST_INVALID_ARGUMENT:
        return "invalid argument";
    case AOS_INST_EOF:
        return "no instruction at end of stream";
    case AOS_INST_INCOMPLETE:
        return "stream ended part-way through an instruction";
    case AOS_INST_TOO_LONG:
        return "instruction exceeds the record budget";
    case AOS_INST_READ_ERROR:
        return "could not read instruction";
    case AOS_INST_EMPTY_ARGV:
        return "instruction has no arguments";
    case AOS_INST_TOO_MANY_ARGS:
        return "instruction has too many arguments";
    case AOS_INST_ALLOC_FAILED:
        return "out of memory";
    case AOS_INST_NULL_ARGUMENT:
        return "instruction contains a NULL argument";
    case AOS_INST_NULL_FIELD:
        return "instruction contains a NULL field";
    case AOS_INST_ARGUMENT_CONTAINS_TAB:
        return "instruction argument contains a tab";
    case AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK:
        return "instruction argument contains a line break";
    case AOS_INST_FIELD_CONTAINS_LINE_BREAK:
        return "instruction field contains a line break";
    case AOS_INST_WRITE_ERROR:
        return "could not write instruction";
    default:
        return "unknown instruction result";
    }
}
