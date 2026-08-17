/*
 * Unit tests for instruction_write / instruction_write_buffer
 * (include/aos/instruction_write.h): rejection paths for invalid input.
 * Serialization and buffer-handling correctness on legal input lives in
 * test_instruction_write.c.
 *
 * No test framework: each check is a plain CHECK() (see test_check.h); a
 * failing check aborts with the file, line and expression, which is enough
 * to find the failing case.
 */
#include "aos/instruction.h"
#include "aos/instruction_write.h"
#include "test_check.h"
#include "test_common.h"

#include <stddef.h>
#include <string.h>

static void test_write_single_empty_arg_rejected(void)
{
    const char *argv[1];
    instruction_t instr;
    char buf[256];
    char *ptr = buf;
    size_t remaining = sizeof(buf);

    argv[0] = "";
    fill_instruction(&instr, argv, 1U);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_EMPTY_ARGV);
    CHECK(ptr == buf);
    CHECK(remaining == sizeof(buf));
}

static void test_write_null_argv_with_nonzero_argc(void)
{
    instruction_t instr;
    char buf[256];
    char *ptr = buf;
    size_t remaining = sizeof(buf);

    fill_instruction(&instr, NULL, 1U);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_NULL_ARGUMENT);
}

static void test_write_argument_contains_tab_or_line_break(void)
{
    const char *argv[1];
    instruction_t instr;
    char buf[256];
    char *ptr;
    size_t remaining;

    argv[0] = "has\ttab";
    fill_instruction(&instr, argv, 1U);
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_ARGUMENT_CONTAINS_TAB);

    argv[0] = "has\nnewline";
    fill_instruction(&instr, argv, 1U);
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_ARGUMENT_CONTAINS_LINE_BREAK);

    argv[0] = "has\rcr";
    fill_instruction(&instr, argv, 1U);
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_ARGUMENT_CONTAINS_LINE_BREAK);
}

static void test_write_field_contains_line_break_or_null(void)
{
    const char *argv[1];
    instruction_t instr;
    char buf[256];
    char *ptr;
    size_t remaining;

    argv[0] = "ok";

    fill_instruction(&instr, argv, 1U);
    instr.cwd = "bad\ncwd";
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_FIELD_CONTAINS_LINE_BREAK);

    fill_instruction(&instr, argv, 1U);
    instr.extra = "bad\rextra";
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_FIELD_CONTAINS_LINE_BREAK);

    fill_instruction(&instr, argv, 1U);
    instr.stdin_path = NULL;
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_NULL_FIELD);
}

static void test_write_empty_and_too_many_argv(void)
{
    const char *many_argv[AOS_INSTRUCTION_ARGV_MAX + 1U];
    instruction_t instr;
    char buf[256];
    char *ptr;
    size_t remaining;
    size_t i;

    fill_instruction(&instr, NULL, 0U);
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_EMPTY_ARGV);

    for (i = 0U; i < AOS_INSTRUCTION_ARGV_MAX + 1U; ++i) {
        many_argv[i] = "x";
    }
    fill_instruction(&instr, many_argv, AOS_INSTRUCTION_ARGV_MAX + 1U);
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_TOO_MANY_ARGUMENTS);
}

static void test_write_buffer_invalid_argument(void)
{
    const char *argv[1];
    instruction_t instr;
    char buf[64];
    char *ptr;
    char *null_ptr;
    size_t remaining;

    argv[0] = "a";
    fill_instruction(&instr, argv, 1U);

    CHECK(instruction_write(NULL, &instr) == INSTRUCTION_WRITE_INVALID_ARGUMENT);

    CHECK(instruction_write_buffer(NULL, &remaining, &instr) ==
           INSTRUCTION_WRITE_INVALID_ARGUMENT);

    null_ptr = NULL;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&null_ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_INVALID_ARGUMENT);

    ptr = buf;
    CHECK(instruction_write_buffer(&ptr, NULL, &instr) ==
           INSTRUCTION_WRITE_INVALID_ARGUMENT);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_write_single_empty_arg_rejected,
    test_write_null_argv_with_nonzero_argc,
    test_write_argument_contains_tab_or_line_break,
    test_write_field_contains_line_break_or_null,
    test_write_empty_and_too_many_argv,
    test_write_buffer_invalid_argument
};

size_t run_instruction_write_error_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
