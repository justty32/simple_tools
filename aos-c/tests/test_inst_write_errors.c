/*
 * aos_inst_write 的驗證拒絕路徑。由於標頭保證會在寫入第一個位元組之前驗證
 * 整筆記錄，因此每次拒絕也都不能改動串流。
 */
#include "test_common.h"

#include <stdlib.h>
#include <string.h>

/* 在全新的 tmpfile 上嘗試寫入一次，並同時檢查回傳狀態及串流未寫入任何內容。 */
static void check_rejected(const aos_inst_t *inst, aos_inst_state expected)
{
    FILE *stream = tmpfile();
    aos_inst_state state;

    CHECK(stream != NULL);
    state = aos_inst_write(stream, inst);

    CHECK(state == expected);
    CHECK(ftell(stream) == 0L);

    fclose(stream);
}

static size_t test_empty_argc_is_empty_argv(void)
{
    aos_inst_t inst;

    fill_inst(&inst, NULL, 0U);
    check_rejected(&inst, AOS_INST_EMPTY_ARGV);
    return 1U;
}

static size_t test_null_argv_with_positive_argc(void)
{
    aos_inst_t inst;

    fill_inst(&inst, NULL, 1U);
    check_rejected(&inst, AOS_INST_NULL_ARGUMENT);
    return 1U;
}

/*
 * 此結果只由 argc 決定：inst_validate 會先依數量拒絕，再存取 argv 的索引，
 * 因此陣列不必真的包含那麼多有效項目，也能安全測試此項檢查。
 */
static size_t test_argc_over_max_is_too_many_args(void)
{
    const char *argv[AOS_INST_ARGV_MAX + 1U];
    aos_inst_t inst;
    size_t i;

    for (i = 0U; i < AOS_INST_ARGV_MAX + 1U; ++i) {
        argv[i] = "a";
    }
    fill_inst(&inst, argv, AOS_INST_ARGV_MAX + 1U);
    check_rejected(&inst, AOS_INST_TOO_MANY_ARGS);
    return 1U;
}

static size_t test_null_entry_inside_argv(void)
{
    const char *argv[] = { "a", NULL };
    aos_inst_t inst;

    fill_inst(&inst, argv, 2U);
    check_rejected(&inst, AOS_INST_NULL_ARGUMENT);
    return 1U;
}

static size_t test_argument_contains_tab(void)
{
    const char *argv[] = { "has\ttab" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    check_rejected(&inst, AOS_INST_ARGUMENT_CONTAINS_TAB);
    return 1U;
}

static size_t test_argument_contains_newline(void)
{
    const char *argv[] = { "has\nbreak" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    check_rejected(&inst, AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK);
    return 1U;
}

static size_t test_argument_contains_cr(void)
{
    const char *argv[] = { "has\rbreak" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    check_rejected(&inst, AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK);
    return 1U;
}

/*
 * 單一空引數沒有定位字元可標示邊界，因此無法往返還原：它會序列化成空白的
 * argv 行，讀回時得到 AOS_INST_EMPTY_ARGV，而不是 argc == 1。寫入器會預先
 * 拒絕它，避免產生無法讀取的記錄。
 */
static size_t test_single_empty_argument_is_empty_argv(void)
{
    const char *argv[] = { "" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    check_rejected(&inst, AOS_INST_EMPTY_ARGV);
    return 1U;
}

/* 每個非 argv 欄位各有一個案例；每次重設一個全新的借用指令，並且只將一個
 * 欄位設為 NULL。 */
static size_t test_null_field_each_of_seven(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;
    size_t field;

    for (field = 0U; field < 7U; ++field) {
        fill_inst(&inst, argv, 1U);
        switch (field) {
        case 0U: inst.stdin_path = NULL; break;
        case 1U: inst.stdout_path = NULL; break;
        case 2U: inst.stderr_path = NULL; break;
        case 3U: inst.exit_path = NULL; break;
        case 4U: inst.cwd = NULL; break;
        case 5U: inst.env_path = NULL; break;
        default: inst.extra = NULL; break;
        }
        check_rejected(&inst, AOS_INST_NULL_FIELD);
    }
    return 1U;
}

static size_t test_field_contains_newline(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    inst.cwd = "has\nbreak";
    check_rejected(&inst, AOS_INST_FIELD_CONTAINS_LINE_BREAK);
    return 1U;
}

static size_t test_field_contains_cr(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    inst.extra = "has\rbreak";
    check_rejected(&inst, AOS_INST_FIELD_CONTAINS_LINE_BREAK);
    return 1U;
}

static size_t test_null_stream_is_invalid_argument(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;
    aos_inst_state state;

    fill_inst(&inst, argv, 1U);
    state = aos_inst_write(NULL, &inst);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);
    return 1U;
}

static size_t test_null_instruction_is_invalid_argument(void)
{
    FILE *stream = tmpfile();
    aos_inst_state state;

    CHECK(stream != NULL);
    state = aos_inst_write(stream, NULL);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);
    CHECK(ftell(stream) == 0L);

    fclose(stream);
    return 1U;
}

/*
 * 手動建構的指令不擁有任何儲存空間：aos_inst_free 不得釋放任何內容
 * （在 ASan 下不會報錯），但仍須將結構歸零。
 */
static size_t test_free_on_borrowed_instruction_is_clean(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    aos_inst_free(&inst);

    CHECK(inst.argc == 0U);
    CHECK(inst.argv == NULL);
    CHECK(inst.stdin_path == NULL);
    CHECK(inst.stdout_path == NULL);
    CHECK(inst.stderr_path == NULL);
    CHECK(inst.exit_path == NULL);
    CHECK(inst.cwd == NULL);
    CHECK(inst.env_path == NULL);
    CHECK(inst.extra == NULL);
    CHECK(inst.storage == NULL);
    CHECK(inst.storage_capacity == 0U);
    CHECK(inst.argv_slots == NULL);
    CHECK(inst.argv_slots_capacity == 0U);

    return 1U;
}

size_t run_inst_write_error_tests(void)
{
    size_t count = 0U;

    count += test_empty_argc_is_empty_argv();
    count += test_null_argv_with_positive_argc();
    count += test_argc_over_max_is_too_many_args();
    count += test_null_entry_inside_argv();
    count += test_argument_contains_tab();
    count += test_argument_contains_newline();
    count += test_argument_contains_cr();
    count += test_single_empty_argument_is_empty_argv();
    count += test_null_field_each_of_seven();
    count += test_field_contains_newline();
    count += test_field_contains_cr();
    count += test_null_stream_is_invalid_argument();
    count += test_null_instruction_is_invalid_argument();
    count += test_free_on_borrowed_instruction_is_clean();

    return count;
}
