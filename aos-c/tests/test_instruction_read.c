/*
 * Unit tests for instruction_read (include/aos/instruction.h): the
 * successful-parse paths. Rejection and boundary cases live in
 * test_instruction_read_errors.c.
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

static void test_read_basic_record(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(buf,
        "echo\thello\n"
        "in.txt\n"
        "out.txt\n"
        "err.txt\n"
        "exit.txt\n"
        "/tmp\n"
        "env.txt\n"
        "extra info\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(instr.argc == 2U);
    CHECK(strcmp(instr.argv[0], "echo") == 0);
    CHECK(strcmp(instr.argv[1], "hello") == 0);
    CHECK(instr.argv[2] == NULL);
    CHECK(strcmp(instr.stdin_path, "in.txt") == 0);
    CHECK(strcmp(instr.stdout_path, "out.txt") == 0);
    CHECK(strcmp(instr.stderr_path, "err.txt") == 0);
    CHECK(strcmp(instr.exit_path, "exit.txt") == 0);
    CHECK(strcmp(instr.cwd, "/tmp") == 0);
    CHECK(strcmp(instr.env_path, "env.txt") == 0);
    CHECK(strcmp(instr.extra, "extra info") == 0);
    CHECK(*cursor == '\0');
}

static void test_read_two_records(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(buf,
        "first\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n"
        "second\targ\n" "h\n" "i\n" "j\n" "k\n" "l\n" "m\n" "n\n");
    cursor = buf;

    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(instr.argc == 1U);
    CHECK(strcmp(instr.argv[0], "first") == 0);
    CHECK(strncmp(cursor, "second", 6U) == 0);

    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(instr.argc == 2U);
    CHECK(strcmp(instr.argv[0], "second") == 0);
    CHECK(strcmp(instr.argv[1], "arg") == 0);
    CHECK(*cursor == '\0');
}

static void test_read_multi_tab_argv(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(buf, "one\ttwo\tthree\tfour\n\n\n\n\n\n\n\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(instr.argc == 4U);
    CHECK(strcmp(instr.argv[0], "one") == 0);
    CHECK(strcmp(instr.argv[1], "two") == 0);
    CHECK(strcmp(instr.argv[2], "three") == 0);
    CHECK(strcmp(instr.argv[3], "four") == 0);
    CHECK(instr.argv[4] == NULL);
}

static void test_read_adjacent_tabs_preserve_empty_args(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(buf, "a\t\tb\n\n\n\n\n\n\n\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(instr.argc == 3U);
    CHECK(strcmp(instr.argv[0], "a") == 0);
    CHECK(strcmp(instr.argv[1], "") == 0);
    CHECK(strcmp(instr.argv[2], "b") == 0);
}

static void test_read_argument_special_chars_are_literal(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(buf, "has space\t\"quoted\"\tback\\slash\n\n\n\n\n\n\n\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(instr.argc == 3U);
    CHECK(strcmp(instr.argv[0], "has space") == 0);
    CHECK(strcmp(instr.argv[1], "\"quoted\"") == 0);
    CHECK(strcmp(instr.argv[2], "back\\slash") == 0);
}

static void test_read_crlf_and_lf_mixed(void)
{
    char buf[256];
    char *cursor;
    const char *slots[SLOTS_COUNT];
    instruction_t instr;
    instruction_state state;

    strcpy(buf,
        "argv\r\n"
        "stdin\r\n"
        "stdout\n"
        "stderr\r\n"
        "exit\n"
        "cwd\r\n"
        "env\n"
        "extra\r\n");
    cursor = buf;
    state = instruction_read(&cursor, &instr, slots, SLOTS_COUNT);
    CHECK(state == INSTRUCTION_OK);
    CHECK(strcmp(instr.argv[0], "argv") == 0);
    CHECK(strchr(instr.argv[0], '\r') == NULL);
    CHECK(strcmp(instr.stdin_path, "stdin") == 0);
    CHECK(strcmp(instr.stdout_path, "stdout") == 0);
    CHECK(strcmp(instr.stderr_path, "stderr") == 0);
    CHECK(strcmp(instr.exit_path, "exit") == 0);
    CHECK(strcmp(instr.cwd, "cwd") == 0);
    CHECK(strcmp(instr.env_path, "env") == 0);
    CHECK(strcmp(instr.extra, "extra") == 0);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_read_basic_record,
    test_read_two_records,
    test_read_multi_tab_argv,
    test_read_adjacent_tabs_preserve_empty_args,
    test_read_argument_special_chars_are_literal,
    test_read_crlf_and_lf_mixed
};

size_t run_instruction_read_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
