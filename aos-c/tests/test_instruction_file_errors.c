/*
 * Unit tests for instruction_file_read's error_index / instruction_error
 * diagnostics (include/aos/instruction_file.h) when the failing record is
 * not the first one, so error_index is verified to be a real index rather
 * than a value that happens to be zero either way. Exports
 * run_instruction_file_error_index_tests().
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

static void check_failed_file_is_empty(const instruction_file_t *file)
{
    CHECK(file->buffer == NULL);
    CHECK(file->instructions == NULL);
    CHECK(file->argv_slots == NULL);
    CHECK(file->count == 0U);
}

static void test_file_error_index_empty_argv_on_second_record(void)
{
    const char *path = SCRATCH_DIR "aos_test_err_empty_argv.instr";
    const char *content =
        "first\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n"
        "\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n";
    instruction_file_t file;

    write_file(path, content, strlen(content));
    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_PARSE_FAILED);
    CHECK(file.error_index == 1U);
    CHECK(file.instruction_error == INSTRUCTION_EMPTY_ARGV);
    check_failed_file_is_empty(&file);
    instruction_file_free(&file);
    remove(path);
}

static void test_file_error_index_too_many_arguments_on_second_record(void)
{
    const char *path = SCRATCH_DIR "aos_test_err_too_many_args.instr";
    const char *good_record =
        "first\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n";
    size_t argc = AOS_INSTRUCTION_ARGV_MAX + 1U;
    size_t prefix_len = strlen(good_record);
    /* argc single-char args, argc - 1 tabs, a newline, and seven more. */
    size_t extra_cap = 2U * argc + 16U;
    char *content = (char *)malloc(prefix_len + extra_cap);
    size_t offset;
    size_t i;
    instruction_file_t file;

    CHECK(content != NULL);
    memcpy(content, good_record, prefix_len);
    offset = prefix_len;
    for (i = 0U; i < argc; ++i) {
        if (i > 0U) {
            content[offset++] = '\t';
        }
        content[offset++] = 'x';
    }
    content[offset++] = '\n';
    for (i = 0U; i < 7U; ++i) {
        content[offset++] = '\n';
    }

    write_file(path, content, offset);
    free(content);

    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_PARSE_FAILED);
    CHECK(file.error_index == 1U);
    CHECK(file.instruction_error == INSTRUCTION_TOO_MANY_ARGUMENTS);
    check_failed_file_is_empty(&file);
    instruction_file_free(&file);
    remove(path);
}

static void test_file_error_index_truncated_on_second_record(void)
{
    const char *path = SCRATCH_DIR "aos_test_err_truncated.instr";
    const char *content =
        "first\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n"
        "second\n" "h\n" "i\n" "j\n" "k\n" "l\n" "m\n";
    instruction_file_t file;

    write_file(path, content, strlen(content));
    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_INCOMPLETE);
    CHECK(file.error_index == 1U);
    CHECK(file.instruction_error == INSTRUCTION_TOO_FEW_LINES);
    check_failed_file_is_empty(&file);
    instruction_file_free(&file);
    remove(path);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_file_error_index_empty_argv_on_second_record,
    test_file_error_index_too_many_arguments_on_second_record,
    test_file_error_index_truncated_on_second_record
};

size_t run_instruction_file_error_index_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
