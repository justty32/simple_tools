/*
 * Unit tests for instruction_read (include/aos/instruction.h): rejection
 * and boundary cases. Successful-parse paths live in
 * test_instruction_read.c.
 *
 * No test framework: each check is a plain CHECK() (see test_check.h); a
 * failing check aborts with the file, line and expression, which is enough
 * to find the failing case. Every buffer handed to instruction_read is a
 * writable stack copy, never a string literal, because the parser rewrites
 * its input in place.
 */
#include "aos/instruction.h"
#include "test_check.h"
#include "test_common.h"

#include <stddef.h>
#include <string.h>

static void test_read_empty_argv(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(buf, "\n\n\n\n\n\n\n\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_EMPTY_ARGV);
    CHECK(instr.argc == 0U);
}

static void test_read_truncated_seven_lines(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    /* Seven newline-terminated lines, then EOF: no eighth line exists. */
    strcpy(buf, "a\nb\nc\nd\ne\nf\ng\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_TOO_FEW_LINES);
}

static void test_read_eighth_line_missing_newline(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    /* Eight lines' worth of content, but the last has no trailing LF. */
    strcpy(buf, "a\nb\nc\nd\ne\nf\ng\nh");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_TOO_FEW_LINES);
}

static void test_read_too_few_lines_short_and_empty(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(buf, "a\nb\nc\nd\ne\nf\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_TOO_FEW_LINES);

    strcpy(buf, "");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_TOO_FEW_LINES);
}

/*
 * Build a record with `argc` single-character tab-separated arguments
 * followed by seven empty field lines, all newline terminated. Returns the
 * byte length written, not counting the terminating NUL.
 */
static size_t build_many_args_record(char *buf, size_t argc)
{
    size_t offset = 0U;
    size_t i;

    for (i = 0U; i < argc; ++i) {
        if (i > 0U) {
            buf[offset++] = '\t';
        }
        buf[offset++] = 'x';
    }
    buf[offset++] = '\n';
    for (i = 0U; i < 7U; ++i) {
        buf[offset++] = '\n';
    }
    buf[offset] = '\0';
    return offset;
}

static void test_read_argv_max_boundary_ok(void)
{
    static char buf[2048];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    build_many_args_record(buf, AOS_INSTRUCTION_ARGV_MAX);
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(instr.argc == AOS_INSTRUCTION_ARGV_MAX);
    CHECK(instr.argv[AOS_INSTRUCTION_ARGV_MAX] == NULL);
}

static void test_read_argv_too_many(void)
{
    static char buf[2048];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    build_many_args_record(buf, AOS_INSTRUCTION_ARGV_MAX + 1U);
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_TOO_MANY_ARGUMENTS);
}

static void test_read_argv_slots_too_small(void)
{
    char buf[256];
    char *cursor;
    const char *slots[2];
    instruction_t instr;
    instruction_state state;

    strcpy(buf, "a\tb\tc\n\n\n\n\n\n\n\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, 2U);
    CHECK(state == INSTRUCTION_TOO_MANY_ARGUMENTS);
}

static void test_read_invalid_arguments(void)
{
    char buf[256];
    char *cursor;
    char *null_cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;

    strcpy(buf, "a\n\n\n\n\n\n\n\n");

    CHECK(instruction_read(NULL, &instr, slots, SLOTS_COUNT) ==
           INSTRUCTION_INVALID_ARGUMENT);

    null_cursor = NULL;
    CHECK(instruction_read(&null_cursor, &instr, slots, SLOTS_COUNT) ==
           INSTRUCTION_INVALID_ARGUMENT);

    cursor = buf;
    CHECK(instruction_read(&cursor, NULL, slots, SLOTS_COUNT) ==
           INSTRUCTION_INVALID_ARGUMENT);

    cursor = buf;
    CHECK(instruction_read(&cursor, &instr, NULL, SLOTS_COUNT) ==
           INSTRUCTION_INVALID_ARGUMENT);

    cursor = buf;
    CHECK(instruction_read(&cursor, &instr, slots, 0U) ==
           INSTRUCTION_INVALID_ARGUMENT);
}

static void test_read_failure_does_not_leak_previous_result(void)
{
    char good[256];
    char bad[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(good, "one\ttwo\n\n\n\n\n\n\n\n");
    cursor = good;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(instr.argc == 2U);

    strcpy(bad, "a\nb\nc\n");
    cursor = bad;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_TOO_FEW_LINES);
    CHECK(instr.argc == 0U);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_read_empty_argv,
    test_read_truncated_seven_lines,
    test_read_eighth_line_missing_newline,
    test_read_too_few_lines_short_and_empty,
    test_read_argv_max_boundary_ok,
    test_read_argv_too_many,
    test_read_argv_slots_too_small,
    test_read_invalid_arguments,
    test_read_failure_does_not_leak_previous_result
};

size_t run_instruction_read_error_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
