/*
 * Unit test for instruction_scan's argv-slot accounting
 * (src/instruction_internal.h via instruction_file_read): Tab bytes in the
 * seven non-argv lines must not be mistaken for argv separators. Exports
 * run_instruction_budget_tabs_tests().
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

static void test_budget_non_argv_tabs_do_not_consume_argv_slots(void)
{
    const char *path = SCRATCH_DIR "aos_test_budget_tabs.instr";
    const char *argv_line = "one\ttwo\n";
    /*
     * Large enough that if a buggy scanner mistook these tabs for argv
     * separators, the bogus argv_slot_count * sizeof(const char *) alone
     * would blow past AOS_INSTRUCTION_FILE_MAX_BYTES in the test build
     * (7 * 2000 * sizeof(const char *) is already many times the cap), so
     * this check has teeth instead of passing vacuously.
     */
    size_t tab_count = 2000U;
    char *field = (char *)malloc(tab_count + 1U);
    char *content;
    size_t offset;
    size_t i, f;
    instruction_file_t file;

    CHECK(field != NULL);
    for (i = 0U; i < tab_count; ++i) {
        field[i] = '\t';
    }
    field[tab_count] = '\0';

    content = (char *)malloc(strlen(argv_line) + 7U * (tab_count + 1U) + 1U);
    CHECK(content != NULL);
    offset = 0U;
    memcpy(content + offset, argv_line, strlen(argv_line));
    offset += strlen(argv_line);
    for (f = 0U; f < 7U; ++f) {
        memcpy(content + offset, field, tab_count);
        offset += tab_count;
        content[offset++] = '\n';
    }
    content[offset] = '\0';

    write_file(path, content, offset);

    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_OK);
    CHECK(file.count == 1U);
    CHECK(file.instructions[0].argc == 2U);
    CHECK(strcmp(file.instructions[0].argv[0], "one") == 0);
    CHECK(strcmp(file.instructions[0].argv[1], "two") == 0);
    /* Every non-argv field got the same tab-only filler; confirm none of
     * it was silently dropped or altered. */
    CHECK(strlen(file.instructions[0].stdin_path) == tab_count);
    CHECK(strcmp(file.instructions[0].stdin_path, field) == 0);
    CHECK(strlen(file.instructions[0].stdout_path) == tab_count);
    CHECK(strcmp(file.instructions[0].stdout_path, field) == 0);
    CHECK(strlen(file.instructions[0].extra) == tab_count);
    CHECK(strcmp(file.instructions[0].extra, field) == 0);

    instruction_file_free(&file);
    free(field);
    free(content);
    remove(path);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_budget_non_argv_tabs_do_not_consume_argv_slots
};

size_t run_instruction_budget_tabs_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
