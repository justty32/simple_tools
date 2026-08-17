/*
 * Unit tests for instruction_file_read / instruction_file_free
 * (include/aos/instruction_file.h). Exports run_instruction_file_tests(),
 * called from main() in test_instruction.c. Same no-framework style: plain
 * CHECK() per check (see test_check.h).
 *
 * Every test that needs a file on disk writes it under the ignored build
 * directory and removes it again before returning. The Makefile creates that
 * directory before launching the test executable.
 */
#include "aos/instruction_file.h"
#include "test_check.h"
#include "test_common.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SCRATCH_DIR "build/"

/* Write exactly `size` bytes to path in binary mode, embedded NULs allowed. */
static void write_file(const char *path, const void *data, size_t size)
{
    FILE *stream = fopen(path, "wb");

    CHECK(stream != NULL);
    if (size > 0U) {
        CHECK(fwrite(data, 1U, size, stream) == size);
    }
    fclose(stream);
}

static void test_file_empty(void)
{
    const char *path = SCRATCH_DIR "aos_test_empty.instr";
    instruction_file_t file;

    write_file(path, "", 0U);
    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_OK);
    CHECK(file.count == 0U);
    instruction_file_free(&file);
    remove(path);
}

static void test_file_two_records(void)
{
    const char *path = SCRATCH_DIR "aos_test_two_records.instr";
    const char *content =
        "first\targ\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n"
        "second\n" "h\n" "i\n" "j\n" "k\n" "l\n" "m\n" "n\n";
    instruction_file_t file;

    write_file(path, content, strlen(content));
    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_OK);
    CHECK(file.count == 2U);

    CHECK(file.instructions[0].argc == 2U);
    CHECK(strcmp(file.instructions[0].argv[0], "first") == 0);
    CHECK(strcmp(file.instructions[0].argv[1], "arg") == 0);
    CHECK(strcmp(file.instructions[0].stdin_path, "a") == 0);
    CHECK(strcmp(file.instructions[0].extra, "g") == 0);

    CHECK(file.instructions[1].argc == 1U);
    CHECK(strcmp(file.instructions[1].argv[0], "second") == 0);
    CHECK(strcmp(file.instructions[1].stdin_path, "h") == 0);
    CHECK(strcmp(file.instructions[1].extra, "n") == 0);

    /* The two records' argv arrays borrow disjoint regions of the shared
     * slot pool, so writing through one must not disturb the other. */
    CHECK(file.instructions[0].argv != file.instructions[1].argv);
    CHECK(strcmp(file.instructions[0].argv[0], "first") == 0);

    instruction_file_free(&file);
    remove(path);
}

static void test_file_truncated_last_record(void)
{
    const char *path = SCRATCH_DIR "aos_test_truncated.instr";
    const char *content =
        "first\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n"
        "second\n" "h\n" "i\n" "j\n" "k\n" "l\n" "m\n";
    instruction_file_t file;

    write_file(path, content, strlen(content));
    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_INCOMPLETE);
    CHECK(file.error_index == 1U);
    CHECK(file.count == 0U);
    instruction_file_free(&file);
    remove(path);
}

static void test_file_contains_nul(void)
{
    const char *path = SCRATCH_DIR "aos_test_nul.instr";
    const unsigned char data[] = { 'a', 'b', '\0', 'c', '\n' };
    instruction_file_t file;

    write_file(path, data, sizeof(data));
    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_CONTAINS_NUL);
    CHECK(file.count == 0U);
    instruction_file_free(&file);
    remove(path);
}

static void test_file_open_failed(void)
{
    const char *path = SCRATCH_DIR "aos_test_does_not_exist.instr";
    instruction_file_t file;

    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_OPEN_FAILED);
    instruction_file_free(&file);
}

static void test_file_invalid_arguments(void)
{
    instruction_file_t file;

    instruction_file_init(&file);
    CHECK(instruction_file_read(NULL, &file) ==
           INSTRUCTION_FILE_INVALID_ARGUMENT);
    CHECK(instruction_file_read(SCRATCH_DIR "unused.instr", NULL) ==
           INSTRUCTION_FILE_INVALID_ARGUMENT);
    instruction_file_free(&file);
}

static void test_file_read_twice_reuses_state(void)
{
    const char *path_a = SCRATCH_DIR "aos_test_reuse_a.instr";
    const char *path_b = SCRATCH_DIR "aos_test_reuse_b.instr";
    const char *content_a =
        "one\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n";
    const char *content_b =
        "two\tmore\n" "h\n" "i\n" "j\n" "k\n" "l\n" "m\n" "n\n";
    instruction_file_t file;

    write_file(path_a, content_a, strlen(content_a));
    write_file(path_b, content_b, strlen(content_b));

    instruction_file_init(&file);
    CHECK(instruction_file_read(path_a, &file) == INSTRUCTION_FILE_OK);
    CHECK(file.count == 1U);
    CHECK(strcmp(file.instructions[0].argv[0], "one") == 0);

    /* Second read must free the first buffer/arrays before reloading. */
    CHECK(instruction_file_read(path_b, &file) == INSTRUCTION_FILE_OK);
    CHECK(file.count == 1U);
    CHECK(file.instructions[0].argc == 2U);
    CHECK(strcmp(file.instructions[0].argv[0], "two") == 0);
    CHECK(strcmp(file.instructions[0].argv[1], "more") == 0);

    instruction_file_free(&file);
    remove(path_a);
    remove(path_b);
}

static void test_file_free_is_idempotent_and_null_safe(void)
{
    const char *path = SCRATCH_DIR "aos_test_free_twice.instr";
    const char *content = "one\n" "a\n" "b\n" "c\n" "d\n" "e\n" "f\n" "g\n";
    instruction_file_t file;

    write_file(path, content, strlen(content));
    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_OK);

    instruction_file_free(&file);
    CHECK(file.buffer == NULL);
    CHECK(file.instructions == NULL);
    CHECK(file.argv_slots == NULL);
    CHECK(file.count == 0U);

    /* Freeing an already-empty file, and freeing NULL, must both be safe. */
    instruction_file_free(&file);
    instruction_file_free(NULL);

    remove(path);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_file_empty,
    test_file_two_records,
    test_file_truncated_last_record,
    test_file_contains_nul,
    test_file_open_failed,
    test_file_invalid_arguments,
    test_file_read_twice_reuses_state,
    test_file_free_is_idempotent_and_null_safe
};

size_t run_instruction_file_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
