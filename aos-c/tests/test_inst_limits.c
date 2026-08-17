/*
 * The two hard limits aos_inst_read enforces: the per-record byte budget
 * and AOS_INST_ARGV_MAX, exercised at and either side of each boundary,
 * plus the buffer-reuse path that keeps storage across records of very
 * different sizes.
 */
#include "test_common.h"

#include <stdlib.h>
#include <string.h>

/*
 * A record whose eight lines (CR and LF excluded) total 8 bytes, so its
 * storage cost is 8 bytes of content plus one NUL per line: 16 bytes.
 * argv is "p\tq" (argc 2), and stdin..cwd are one byte each; env and extra
 * are empty.
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

/* Build an argv line of `argc` single-character arguments separated by
 * tabs, followed by seven empty lines, giving a complete eight-line
 * record. Caller frees the result. */
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

/* Tabs on lines 2-8 are just bytes to the reader; only line 1 is split. */
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

/* One instruction, three records of very different lengths back to back:
 * long, then short, then long again. This exercises storage and argv_slots
 * capacity being kept and reused rather than reset each read. */
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
