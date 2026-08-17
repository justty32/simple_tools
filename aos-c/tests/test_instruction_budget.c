/*
 * Unit tests for instruction_file_read's total memory budget
 * (include/aos/instruction_file.h): the boundary at exactly
 * AOS_INSTRUCTION_FILE_MAX_BYTES and one byte past it. Both derive the
 * padding they need from AOS_INSTRUCTION_FILE_MAX_BYTES and sizeof(), never
 * a literal, so the test still holds if the Makefile's test-build override
 * changes. Exports run_instruction_budget_tests().
 */
#include "aos/instruction_file.h"
#include "test_check.h"
#include "test_common.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH_DIR "build/"

static void write_file(const char *path, const void *data, size_t size)
{
    FILE *stream = fopen(path, "wb");

    CHECK(stream != NULL);
    if (size > 0U) {
        CHECK(fwrite(data, 1U, size, stream) == size);
    }
    fclose(stream);
}

/*
 * Build a one-argument record ("a" plus six empty fields) whose eighth
 * (extra) line is padded with `pad` filler bytes before its newline, and
 * write it to path. Returns the total file size.
 */
static size_t write_padded_single_arg_record(const char *path, size_t pad)
{
    size_t size = 9U + pad; /* "a\n" + six "\n" + pad filler + "\n" */
    char *content = (char *)malloc(size);
    size_t i;

    CHECK(content != NULL);
    content[0] = 'a';
    content[1] = '\n';
    for (i = 0U; i < 6U; ++i) {
        content[2U + i] = '\n';
    }
    for (i = 0U; i < pad; ++i) {
        content[8U + i] = 'x';
    }
    content[8U + pad] = '\n';

    write_file(path, content, size);
    free(content);
    return size;
}

static void test_budget_exact_at_cap_ok(void)
{
    const char *path = SCRATCH_DIR "aos_test_budget_exact.instr";
    /* One instruction with argc == 1 needs one instruction_t plus argc + 1
     * (== 2) argv slots; see instruction_file_over_budget's formula. */
    size_t metadata = 1U * sizeof(instruction_t) + 2U * sizeof(const char *);
    size_t base = 9U;
    size_t pad = (size_t)AOS_INSTRUCTION_FILE_MAX_BYTES - 1U - metadata - base;
    instruction_file_t file;

    write_padded_single_arg_record(path, pad);

    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_OK);
    CHECK(file.count == 1U);
    CHECK(file.instructions[0].argc == 1U);
    CHECK(strcmp(file.instructions[0].argv[0], "a") == 0);
    instruction_file_free(&file);
    remove(path);
}

static void test_budget_one_byte_over_cap_too_large(void)
{
    const char *path = SCRATCH_DIR "aos_test_budget_over.instr";
    size_t metadata = 1U * sizeof(instruction_t) + 2U * sizeof(const char *);
    size_t base = 9U;
    size_t pad =
        (size_t)AOS_INSTRUCTION_FILE_MAX_BYTES - 1U - metadata - base + 1U;
    size_t size;
    instruction_file_t file;

    size = write_padded_single_arg_record(path, pad);

    /*
     * The file's own bytes (file_size + 1 for the parser's NUL) are still
     * comfortably under the cap; only after adding the instruction and
     * argv_slots metadata does the total cross it. So a TOO_LARGE result
     * here can only come from the post-scan budget check, not from the
     * file-size precheck in instruction_file_load.
     */
    CHECK(size + 1U <= (size_t)AOS_INSTRUCTION_FILE_MAX_BYTES);

    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_TOO_LARGE);
    CHECK(file.buffer == NULL);
    CHECK(file.instructions == NULL);
    CHECK(file.argv_slots == NULL);
    CHECK(file.count == 0U);
    instruction_file_free(&file);
    remove(path);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_budget_exact_at_cap_ok,
    test_budget_one_byte_over_cap_too_large
};

size_t run_instruction_budget_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
