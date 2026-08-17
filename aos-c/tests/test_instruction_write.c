/*
 * Unit tests for instruction_write / instruction_write_buffer
 * (include/aos/instruction_write.h): serialization and buffer-handling
 * correctness on legal input. Rejection paths live in
 * test_instruction_write_errors.c.
 *
 * No test framework: each check is a plain CHECK() (see test_check.h); a
 * failing check aborts with the file, line and expression, which is enough
 * to find the failing case. Round-trip checks read a written record back
 * with instruction_read into a writable stack buffer, never a string
 * literal, because the parser rewrites its input in place.
 */
#include "aos/instruction.h"
#include "aos/instruction_write.h"
#include "test_check.h"
#include "test_common.h"

#include <stddef.h>
#include <string.h>

static void test_write_round_trip(void)
{
    const char *argv[3];
    instruction_t instr;
    instruction_t read_instr;
    char buf[256];
    char *ptr;
    char *cursor;
    size_t remaining;
    const char *slots[SLOTS_COUNT];

    argv[0] = "a";
    argv[1] = "";
    argv[2] = "b with space";
    fill_instruction(&instr, argv, 3U);
    instr.stdin_path = "in.txt";
    instr.stdout_path = "";
    instr.stderr_path = "err.txt";
    instr.exit_path = "exit.txt";
    instr.cwd = "/some/dir";
    instr.env_path = "";
    instr.extra = "misc";

    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_OK);

    cursor = buf;
    CHECK(instruction_read(&cursor, &read_instr, slots, SLOTS_COUNT) ==
           INSTRUCTION_OK);
    CHECK(read_instr.argc == 3U);
    CHECK(strcmp(read_instr.argv[0], "a") == 0);
    CHECK(strcmp(read_instr.argv[1], "") == 0);
    CHECK(strcmp(read_instr.argv[2], "b with space") == 0);
    CHECK(strcmp(read_instr.stdin_path, "in.txt") == 0);
    CHECK(strcmp(read_instr.stdout_path, "") == 0);
    CHECK(strcmp(read_instr.stderr_path, "err.txt") == 0);
    CHECK(strcmp(read_instr.exit_path, "exit.txt") == 0);
    CHECK(strcmp(read_instr.cwd, "/some/dir") == 0);
    CHECK(strcmp(read_instr.env_path, "") == 0);
    CHECK(strcmp(read_instr.extra, "misc") == 0);
}

static void test_write_trailing_empty_arg_ok(void)
{
    const char *argv[2];
    instruction_t instr;
    instruction_t read_instr;
    char buf[256];
    char *ptr = buf;
    char *cursor;
    size_t remaining = sizeof(buf);
    const char *slots[SLOTS_COUNT];

    argv[0] = "a";
    argv[1] = "";
    fill_instruction(&instr, argv, 2U);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_OK);

    cursor = buf;
    CHECK(instruction_read(&cursor, &read_instr, slots, SLOTS_COUNT) ==
           INSTRUCTION_OK);
    CHECK(read_instr.argc == 2U);
    CHECK(strcmp(read_instr.argv[0], "a") == 0);
    CHECK(strcmp(read_instr.argv[1], "") == 0);
}

static void test_write_buffer_capacity_boundary(void)
{
    const char *argv[2];
    instruction_t instr;
    static char big[512];
    char exact[512];
    char short_buf[512];
    char *ptr;
    size_t remaining;
    size_t serialized_size;

    argv[0] = "arg";
    argv[1] = "second";
    fill_instruction(&instr, argv, 2U);
    instr.cwd = "cwd";

    /* Learn the exact serialized size from a generously sized buffer. */
    ptr = big;
    remaining = sizeof(big);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_OK);
    serialized_size = sizeof(big) - remaining;

    /* Exactly serialized_size + 1 bytes of room must succeed. */
    memset(exact, 0xAA, sizeof(exact));
    ptr = exact;
    remaining = serialized_size + 1U;
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_OK);
    CHECK(remaining == 1U);
    CHECK(ptr == exact + serialized_size);

    /* One byte less must fail, leaving ptr, remaining and buffer untouched. */
    memset(short_buf, 0xAA, sizeof(short_buf));
    ptr = short_buf;
    remaining = serialized_size;
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_BUFFER_TOO_SMALL);
    CHECK(ptr == short_buf);
    CHECK(remaining == serialized_size);
    {
        size_t i;
        for (i = 0U; i < sizeof(short_buf); ++i) {
            CHECK((unsigned char)short_buf[i] == 0xAAU);
        }
    }
}

static void test_write_buffer_appends_two_records(void)
{
    const char *argv1[1];
    const char *argv2[1];
    instruction_t instr;
    instruction_t read_instr;
    char buf[512];
    char *ptr;
    char *cursor;
    size_t remaining;
    const char *slots[SLOTS_COUNT];

    argv1[0] = "first";
    fill_instruction(&instr, argv1, 1U);
    ptr = buf;
    remaining = sizeof(buf);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_OK);

    argv2[0] = "second";
    fill_instruction(&instr, argv2, 1U);
    CHECK(instruction_write_buffer(&ptr, &remaining, &instr) ==
           INSTRUCTION_WRITE_OK);

    cursor = buf;
    CHECK(instruction_read(&cursor, &read_instr, slots, SLOTS_COUNT) ==
           INSTRUCTION_OK);
    CHECK(strcmp(read_instr.argv[0], "first") == 0);
    CHECK(instruction_read(&cursor, &read_instr, slots, SLOTS_COUNT) ==
           INSTRUCTION_OK);
    CHECK(strcmp(read_instr.argv[0], "second") == 0);
    CHECK(*cursor == '\0');
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_write_round_trip,
    test_write_trailing_empty_arg_ok,
    test_write_buffer_capacity_boundary,
    test_write_buffer_appends_two_records
};

size_t run_instruction_write_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
