/*
 * aos_inst_read 的成功路徑：欄位擷取、argv 分割規則、CRLF 處理，以及使用
 * 同一個指令讀取多筆記錄。
 */
#include "test_common.h"

#include <stdlib.h>
#include <string.h>

/* 建立串流並對其執行一次 aos_inst_read，再將串流交還給呼叫端，以便關閉它，
 * 或在含多筆記錄的案例中再次讀取。 */
static FILE *read_one(const char *text, aos_inst_t *inst, aos_inst_state *state)
{
    FILE *stream = stream_from(text);

    aos_inst_init(inst);
    *state = aos_inst_read(stream, inst);
    return stream;
}

static size_t test_valid_record(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = read_one(
        "prog\targ1\targ2\n"
        "/dev/null\n"
        "/tmp/out\n"
        "/tmp/err\n"
        "0\n"
        "/home/user\n"
        "/etc/env\n"
        "note\n",
        &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 3U);
    CHECK(strcmp(inst.argv[0], "prog") == 0);
    CHECK(strcmp(inst.argv[1], "arg1") == 0);
    CHECK(strcmp(inst.argv[2], "arg2") == 0);
    CHECK(inst.argv[3] == NULL);
    CHECK(strcmp(inst.stdin_path, "/dev/null") == 0);
    CHECK(strcmp(inst.stdout_path, "/tmp/out") == 0);
    CHECK(strcmp(inst.stderr_path, "/tmp/err") == 0);
    CHECK(strcmp(inst.exit_path, "0") == 0);
    CHECK(strcmp(inst.cwd, "/home/user") == 0);
    CHECK(strcmp(inst.env_path, "/etc/env") == 0);
    CHECK(strcmp(inst.extra, "note") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_argv_three_args(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = read_one("a\tb\tc\n\n\n\n\n\n\n\n", &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 3U);
    CHECK(strcmp(inst.argv[0], "a") == 0);
    CHECK(strcmp(inst.argv[1], "b") == 0);
    CHECK(strcmp(inst.argv[2], "c") == 0);
    CHECK(inst.argv[3] == NULL);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_argv_adjacent_tabs_empty_middle(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = read_one("a\t\tc\n\n\n\n\n\n\n\n", &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 3U);
    CHECK(strcmp(inst.argv[0], "a") == 0);
    CHECK(strcmp(inst.argv[1], "") == 0);
    CHECK(strcmp(inst.argv[2], "c") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_argv_trailing_tab_empty_last(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = read_one("a\tb\t\n\n\n\n\n\n\n\n", &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 3U);
    CHECK(strcmp(inst.argv[0], "a") == 0);
    CHECK(strcmp(inst.argv[1], "b") == 0);
    CHECK(strcmp(inst.argv[2], "") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_argv_single_no_tab(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = read_one("solo\n\n\n\n\n\n\n\n", &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 1U);
    CHECK(strcmp(inst.argv[0], "solo") == 0);
    CHECK(inst.argv[1] == NULL);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_non_argv_lines_all_empty(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = read_one("cmd\n\n\n\n\n\n\n\n", &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(strcmp(inst.stdin_path, "") == 0);
    CHECK(strcmp(inst.stdout_path, "") == 0);
    CHECK(strcmp(inst.stderr_path, "") == 0);
    CHECK(strcmp(inst.exit_path, "") == 0);
    CHECK(strcmp(inst.cwd, "") == 0);
    CHECK(strcmp(inst.env_path, "") == 0);
    CHECK(strcmp(inst.extra, "") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_crlf_stripped_everywhere(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = read_one(
        "one\ttwo\r\n"
        "in\r\n"
        "out\r\n"
        "err\r\n"
        "0\r\n"
        "/tmp\r\n"
        "env\r\n"
        "extra\r\n",
        &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 2U);
    CHECK(strcmp(inst.argv[0], "one") == 0);
    CHECK(strcmp(inst.argv[1], "two") == 0);
    CHECK(strcmp(inst.stdin_path, "in") == 0);
    CHECK(strcmp(inst.stdout_path, "out") == 0);
    CHECK(strcmp(inst.stderr_path, "err") == 0);
    CHECK(strcmp(inst.exit_path, "0") == 0);
    CHECK(strcmp(inst.cwd, "/tmp") == 0);
    CHECK(strcmp(inst.env_path, "env") == 0);
    CHECK(strcmp(inst.extra, "extra") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

/* 後方未緊接 LF 的 CR 並不是 CRLF 組合，因此讀取器必須將它保留為一般資料，
 * 而不是將它移除。 */
static size_t test_bare_cr_kept_as_data(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = read_one("cmd\n\n\n\n\n\n\nab\rcd\n", &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(strcmp(inst.extra, "ab\rcd") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

/* 使用同一個指令讀取兩筆記錄：第二次讀取必須完整取代第一次的內容，不得留下
 * 過時的位元組或 argv 欄位，而第三次讀取必須回報正常的 EOF。 */
static size_t test_reuse_no_aliasing_then_eof(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from(
        "one\ttwo\tthree\n"
        "in1\n"
        "out1\n"
        "err1\n"
        "ex1\n"
        "cwd1\n"
        "env1\n"
        "extra1\n"
        "x\ty\n"
        "in2\n"
        "out2\n"
        "err2\n"
        "ex2\n"
        "cwd2\n"
        "env2\n"
        "extra2\n");

    aos_inst_init(&inst);

    state = aos_inst_read(stream, &inst);
    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 3U);
    CHECK(strcmp(inst.argv[0], "one") == 0);
    CHECK(strcmp(inst.extra, "extra1") == 0);

    /* 第二筆記錄的引數較少、欄位值也較短：這會測試 argv_slots 與 storage 的
     * 重複使用路徑。 */
    state = aos_inst_read(stream, &inst);
    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 2U);
    CHECK(strcmp(inst.argv[0], "x") == 0);
    CHECK(strcmp(inst.argv[1], "y") == 0);
    CHECK(inst.argv[2] == NULL);
    CHECK(strcmp(inst.stdin_path, "in2") == 0);
    CHECK(strcmp(inst.stdout_path, "out2") == 0);
    CHECK(strcmp(inst.stderr_path, "err2") == 0);
    CHECK(strcmp(inst.exit_path, "ex2") == 0);
    CHECK(strcmp(inst.cwd, "cwd2") == 0);
    CHECK(strcmp(inst.env_path, "env2") == 0);
    CHECK(strcmp(inst.extra, "extra2") == 0);

    state = aos_inst_read(stream, &inst);
    CHECK(state == AOS_INST_EOF);
    CHECK(inst.argc == 0U);
    CHECK(inst.argv == NULL);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_ordinary_punctuation_in_argument(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    const char *expected = "he said \"hi\" \\ backslash 'quote'";
    FILE *stream;
    char text[256];

    /* 以原樣、未跳脫的引數值建立記錄；格式不使用引號或跳脫，因此往返後必須
     * 逐字元保持不變。 */
    (void)snprintf(text, sizeof(text), "%s\n\n\n\n\n\n\n\n", expected);
    stream = read_one(text, &inst, &state);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 1U);
    CHECK(strcmp(inst.argv[0], expected) == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

size_t run_inst_read_tests(void)
{
    size_t count = 0U;

    count += test_valid_record();
    count += test_argv_three_args();
    count += test_argv_adjacent_tabs_empty_middle();
    count += test_argv_trailing_tab_empty_last();
    count += test_argv_single_no_tab();
    count += test_non_argv_lines_all_empty();
    count += test_crlf_stripped_everywhere();
    count += test_bare_cr_kept_as_data();
    count += test_reuse_no_aliasing_then_eof();
    count += test_ordinary_punctuation_in_argument();

    return count;
}
