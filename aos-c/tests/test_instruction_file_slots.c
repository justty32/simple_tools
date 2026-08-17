/*
 * Unit test for instruction_file_read's argv_slots layout
 * (include/aos/instruction_file.h): with several records of differing argc,
 * each record's borrowed argv span must be exact and the records' spans
 * must abut with neither gap nor overlap. Exports
 * run_instruction_file_slots_tests().
 */
#include "aos/instruction_file.h"
#include "test_check.h"
#include "test_common.h"

#include <stddef.h>
#include <stdio.h>
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

static void test_file_multi_record_slots_disjoint_and_roundtrip(void)
{
    const char *path = SCRATCH_DIR "aos_test_multi_slots.instr";
    const char *content =
        "one\n" "a1\n" "b1\n" "c1\n" "d1\n" "e1\n" "f1\n" "g1\n"
        "two\tthree\tfour\n" "a2\n" "b2\n" "c2\n" "d2\n" "e2\n" "f2\n" "g2\n"
        "five\tsix\n" "a3\n" "b3\n" "c3\n" "d3\n" "e3\n" "f3\n" "g3\n";
    instruction_file_t file;

    write_file(path, content, strlen(content));
    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_OK);
    CHECK(file.count == 3U);

    CHECK(file.instructions[0].argc == 1U);
    CHECK(strcmp(file.instructions[0].argv[0], "one") == 0);
    CHECK(file.instructions[0].argv[1] == NULL);

    CHECK(file.instructions[1].argc == 3U);
    CHECK(strcmp(file.instructions[1].argv[0], "two") == 0);
    CHECK(strcmp(file.instructions[1].argv[1], "three") == 0);
    CHECK(strcmp(file.instructions[1].argv[2], "four") == 0);
    CHECK(file.instructions[1].argv[3] == NULL);

    CHECK(file.instructions[2].argc == 2U);
    CHECK(strcmp(file.instructions[2].argv[0], "five") == 0);
    CHECK(strcmp(file.instructions[2].argv[1], "six") == 0);
    CHECK(file.instructions[2].argv[2] == NULL);

    /* Each record's argv slot span is exactly argc + 1 (its entries plus
     * the trailing NULL); adjacent records must abut exactly, with no slot
     * wasted and none shared. */
    CHECK(file.instructions[0].argv + file.instructions[0].argc + 1U ==
          file.instructions[1].argv);
    CHECK(file.instructions[1].argv + file.instructions[1].argc + 1U ==
          file.instructions[2].argv);

    instruction_file_free(&file);
    remove(path);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_file_multi_record_slots_disjoint_and_roundtrip
};

size_t run_instruction_file_slots_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
