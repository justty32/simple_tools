/*
 * aos_inst_read 強制執行的兩項硬性限制：單筆記錄位元組上限與
 * AOS_INST_ARGV_MAX。本檔案測試各項限制的邊界值及其前後，並測試緩衝區
 * 重複使用路徑，確認大小差異很大的記錄之間仍會保留儲存空間。
 */
#include "test_common.h"

#include <stdlib.h>
#include <string.h>

/*
 * 此記錄的八行內容（不含 CR 與 LF）合計 8 個位元組，因此儲存成本為 8 個
 * 內容位元組，加上每行一個 NUL，共 16 個位元組。argv 為 "p\tq"
 * （argc 為 2），stdin 到 cwd 各占一個位元組；env 與 extra 為空。
 */
#define LIMIT_RECORD_TEXT "p\tq\ni\no\ne\nx\nc\n\n\n"
#define LIMIT_RECORD_BYTES 16U

static size_t test_budget_exact_fits(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from(LIMIT_RECORD_TEXT);

    aos_inst_init(&inst);
    state = aos_inst_read_max(stream, &inst, LIMIT_RECORD_BYTES);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 2U);
    CHECK(strcmp(inst.argv[0], "p") == 0);
    CHECK(strcmp(inst.argv[1], "q") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_budget_one_byte_short(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from(LIMIT_RECORD_TEXT);

    aos_inst_init(&inst);
    state = aos_inst_read_max(stream, &inst, LIMIT_RECORD_BYTES - 1U);

    CHECK(state == AOS_INST_TOO_LONG);
    CHECK(inst.argc == 0U);
    CHECK(inst.argv == NULL);
    CHECK(inst.stdin_path == NULL);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

/* 建立一個 argv 行，其中包含 `argc` 個以定位字元分隔的單字元引數，接著附加
 * 七個空白行，形成完整的八行記錄。結果由呼叫端釋放。 */
static char *build_argv_record(size_t argc)
{
    size_t capacity = argc * 2U + 16U;
    char *buf = (char *)malloc(capacity);
    size_t pos = 0U;
    size_t i;

    CHECK(buf != NULL);
    for (i = 0U; i < argc; ++i) {
        if (i > 0U) {
            buf[pos++] = '\t';
        }
        buf[pos++] = 'a';
    }
    buf[pos++] = '\n';
    for (i = 0U; i < 7U; ++i) {
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return buf;
}

static size_t test_argv_max_count_ok(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    char *text = build_argv_record(AOS_INST_ARGV_MAX);
    FILE *stream = stream_from(text);

    free(text);
    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == AOS_INST_ARGV_MAX);
    CHECK(strcmp(inst.argv[0], "a") == 0);
    CHECK(strcmp(inst.argv[AOS_INST_ARGV_MAX - 1U], "a") == 0);
    CHECK(inst.argv[AOS_INST_ARGV_MAX] == NULL);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_argv_over_max_count(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    char *text = build_argv_record(AOS_INST_ARGV_MAX + 1U);
    FILE *stream = stream_from(text);

    free(text);
    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_TOO_MANY_ARGS);
    CHECK(inst.argc == 0U);
    CHECK(inst.argv == NULL);
    CHECK(inst.stdin_path == NULL);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_argv_max_matches_accessor(void)
{
    CHECK(aos_inst_argv_max() == AOS_INST_ARGV_MAX);
    return 1U;
}

/* 對讀取器而言，第 2 至第 8 行的定位字元只是一般位元組；只有第 1 行會分割。 */
static size_t test_tabs_in_non_argv_lines_are_data(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("cmd\nin\nout\nerr\nex\na\tb\tc\nenv\nextra\n");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 1U);
    CHECK(strcmp(inst.cwd, "a\tb\tc") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

/* 使用一個指令連續讀取三筆長度差異很大的記錄：先長、再短、最後再長。
 * 此案例確認 storage 與 argv_slots 的容量會保留並重複使用，而不是每次讀取時
 * 重設。 */
static size_t test_buffer_reuse_across_varied_sizes(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from(
        "alpha\tbeta\tgamma\n"
        "/very/long/path/one\n"
        "/very/long/path/two\n"
        "/very/long/path/three\n"
        "0\n"
        "/very/long/cwd/path\n"
        "/very/long/env/path\n"
        "some long extra content here\n"
        "x\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "one\ttwo\tthree\tfour\n"
        "/a\n"
        "/b\n"
        "/c\n"
        "1\n"
        "/d\n"
        "/e\n"
        "f-final\n");

    aos_inst_init(&inst);

    state = aos_inst_read(stream, &inst);
    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 3U);
    CHECK(strcmp(inst.argv[0], "alpha") == 0);
    CHECK(strcmp(inst.argv[1], "beta") == 0);
    CHECK(strcmp(inst.argv[2], "gamma") == 0);
    CHECK(strcmp(inst.stdin_path, "/very/long/path/one") == 0);
    CHECK(strcmp(inst.stdout_path, "/very/long/path/two") == 0);
    CHECK(strcmp(inst.stderr_path, "/very/long/path/three") == 0);
    CHECK(strcmp(inst.exit_path, "0") == 0);
    CHECK(strcmp(inst.cwd, "/very/long/cwd/path") == 0);
    CHECK(strcmp(inst.env_path, "/very/long/env/path") == 0);
    CHECK(strcmp(inst.extra, "some long extra content here") == 0);

    state = aos_inst_read(stream, &inst);
    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 1U);
    CHECK(strcmp(inst.argv[0], "x") == 0);
    CHECK(strcmp(inst.stdin_path, "") == 0);
    CHECK(strcmp(inst.stdout_path, "") == 0);
    CHECK(strcmp(inst.stderr_path, "") == 0);
    CHECK(strcmp(inst.exit_path, "") == 0);
    CHECK(strcmp(inst.cwd, "") == 0);
    CHECK(strcmp(inst.env_path, "") == 0);
    CHECK(strcmp(inst.extra, "") == 0);

    state = aos_inst_read(stream, &inst);
    CHECK(state == AOS_INST_OK);
    CHECK(inst.argc == 4U);
    CHECK(strcmp(inst.argv[0], "one") == 0);
    CHECK(strcmp(inst.argv[1], "two") == 0);
    CHECK(strcmp(inst.argv[2], "three") == 0);
    CHECK(strcmp(inst.argv[3], "four") == 0);
    CHECK(inst.argv[4] == NULL);
    CHECK(strcmp(inst.stdin_path, "/a") == 0);
    CHECK(strcmp(inst.stdout_path, "/b") == 0);
    CHECK(strcmp(inst.stderr_path, "/c") == 0);
    CHECK(strcmp(inst.exit_path, "1") == 0);
    CHECK(strcmp(inst.cwd, "/d") == 0);
    CHECK(strcmp(inst.env_path, "/e") == 0);
    CHECK(strcmp(inst.extra, "f-final") == 0);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

size_t run_inst_limit_tests(void)
{
    size_t count = 0U;

    count += test_budget_exact_fits();
    count += test_budget_one_byte_short();
    count += test_argv_max_count_ok();
    count += test_argv_over_max_count();
    count += test_argv_max_matches_accessor();
    count += test_tabs_in_non_argv_lines_are_data();
    count += test_buffer_reuse_across_varied_sizes();

    return count;
}
