/*
 * aos_inst_read 的拒絕路徑：EOF、INCOMPLETE、EMPTY_ARGV、無效引數、
 * 讀取錯誤，以及 aos_inst_state_string 的涵蓋範圍。
 */
#include "test_common.h"

#include <string.h>

/* 根據標頭所定義的契約，每次失敗都必須讓指令保持為空。 */
static void check_empty(const aos_inst_t *inst)
{
    CHECK(inst->argc == 0U);
    CHECK(inst->argv == NULL);
    CHECK(inst->stdin_path == NULL);
}

static size_t test_empty_stream_is_eof(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_EOF);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_five_lines_then_eof(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("cmd\n" "in\n" "out\n" "err\n" "ex\n");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_INCOMPLETE);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_seven_lines_then_eof(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from(
        "cmd\n" "in\n" "out\n" "err\n" "ex\n" "cwd\n" "env\n");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_INCOMPLETE);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

/* 八行都存在，但最後一行沒有結尾換行字元：串流在一行中途結束，而不是在記錄
 * 中途的行與行之間結束，因此會走不同的程式碼路徑。 */
static size_t test_eighth_line_missing_newline(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from(
        "cmd\n" "in\n" "out\n" "err\n" "ex\n" "cwd\n" "env\n" "extra");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_INCOMPLETE);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_empty_argv_line(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("\nin\nout\nerr\nex\ncwd\nenv\nextra\n");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_EMPTY_ARGV);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_null_stream_is_invalid_argument(void)
{
    aos_inst_t inst;
    aos_inst_state state;

    aos_inst_init(&inst);
    state = aos_inst_read(NULL, &inst);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);
    check_empty(&inst);

    aos_inst_free(&inst);
    return 1U;
}

static size_t test_null_instruction_is_invalid_argument(void)
{
    aos_inst_state state;
    FILE *stream = stream_from("cmd\n\n\n\n\n\n\n\n");

    state = aos_inst_read(stream, NULL);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);

    fclose(stream);
    return 1U;
}

static size_t test_zero_budget_is_invalid_argument(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("cmd\n\n\n\n\n\n\n\n");

    aos_inst_init(&inst);
    state = aos_inst_read_max(stream, &inst, 0U);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

/*
 * 只以寫入模式開啟的串流無法滿足 getc，而 C 標準將「設定錯誤指示器」或
 * 「僅回報檔案結尾」交由實作決定。在 glibc 等會設定錯誤指示器的環境中，
 * 這是不需真正發生 I/O 錯誤便能觸發 AOS_INST_READ_ERROR 的唯一路徑。
 * 若某個實作只回報 EOF，此處便會回傳 AOS_INST_EOF，導致本案例失敗；
 * 這是測試的可移植性限制，而不是讀取器的限制。
 */
static size_t test_write_only_stream_is_read_error(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    const char *path = "aos_inst_read_errors_write_only.tmp";
    FILE *stream = fopen(path, "w");

    CHECK(stream != NULL);
    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_READ_ERROR);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    (void)remove(path);
    return 1U;
}

static size_t test_state_string_never_empty(void)
{
    aos_inst_state state;

    for (state = AOS_INST_OK; state <= AOS_INST_WRITE_ERROR;
         state = (aos_inst_state)((int)state + 1)) {
        const char *text = aos_inst_state_string(state);

        CHECK(text != NULL);
        CHECK(text[0] != '\0');
    }

    /* 超出範圍的值仍必須取得說明，而不是 NULL 或空字串。 */
    CHECK(aos_inst_state_string((aos_inst_state)9999) != NULL);
    CHECK(aos_inst_state_string((aos_inst_state)9999)[0] != '\0');

    return 1U;
}

size_t run_inst_read_error_tests(void)
{
    size_t count = 0U;

    count += test_empty_stream_is_eof();
    count += test_five_lines_then_eof();
    count += test_seven_lines_then_eof();
    count += test_eighth_line_missing_newline();
    count += test_empty_argv_line();
    count += test_null_stream_is_invalid_argument();
    count += test_null_instruction_is_invalid_argument();
    count += test_zero_budget_is_invalid_argument();
    count += test_write_only_stream_is_read_error();
    count += test_state_string_never_empty();

    return count;
}
