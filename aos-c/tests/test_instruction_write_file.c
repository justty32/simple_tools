/*
 * Unit test for instruction_write(FILE *, ...)'s success path
 * (include/aos/instruction_write.h). Only its NULL-stream rejection was
 * previously covered by test_instruction_write_errors.c; this exercises
 * writing several real records and reading them back. Exports
 * run_instruction_write_file_tests().
 */
#include "aos/instruction.h"
#include "aos/instruction_file.h"
#include "aos/instruction_write.h"
#include "test_check.h"
#include "test_common.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SCRATCH_DIR "build/"

static void test_write_file_two_records_roundtrip(void)
{
    const char *path = SCRATCH_DIR "aos_test_write_file.instr";
    const char *argv1[3];
    const char *argv2[2];
    instruction_t instr;
    instruction_file_t file;
    FILE *stream;

    argv1[0] = "run";
    argv1[1] = "";
    argv1[2] = "arg two";
    fill_instruction(&instr, argv1, 3U);
    instr.stdin_path = "in1.txt";
    instr.stdout_path = "";
    instr.stderr_path = "err1.txt";
    instr.exit_path = "exit1.txt";
    instr.cwd = "/first/dir";
    instr.env_path = "env1.txt";
    instr.extra = "extra one";

    stream = fopen(path, "wb");
    CHECK(stream != NULL);
    CHECK(instruction_write(stream, &instr) == INSTRUCTION_WRITE_OK);

    argv2[0] = "second-cmd";
    argv2[1] = "only-arg";
    fill_instruction(&instr, argv2, 2U);
    instr.stdin_path = "";
    instr.stdout_path = "out2.txt";
    instr.stderr_path = "";
    instr.exit_path = "exit2.txt";
    instr.cwd = "";
    instr.env_path = "env2.txt";
    instr.extra = "";

    CHECK(instruction_write(stream, &instr) == INSTRUCTION_WRITE_OK);
    CHECK(fclose(stream) == 0);

    instruction_file_init(&file);
    CHECK(instruction_file_read(path, &file) == INSTRUCTION_FILE_OK);
    CHECK(file.count == 2U);

    CHECK(file.instructions[0].argc == 3U);
    CHECK(strcmp(file.instructions[0].argv[0], "run") == 0);
    CHECK(strcmp(file.instructions[0].argv[1], "") == 0);
    CHECK(strcmp(file.instructions[0].argv[2], "arg two") == 0);
    CHECK(strcmp(file.instructions[0].stdin_path, "in1.txt") == 0);
    CHECK(strcmp(file.instructions[0].stdout_path, "") == 0);
    CHECK(strcmp(file.instructions[0].stderr_path, "err1.txt") == 0);
    CHECK(strcmp(file.instructions[0].exit_path, "exit1.txt") == 0);
    CHECK(strcmp(file.instructions[0].cwd, "/first/dir") == 0);
    CHECK(strcmp(file.instructions[0].env_path, "env1.txt") == 0);
    CHECK(strcmp(file.instructions[0].extra, "extra one") == 0);

    CHECK(file.instructions[1].argc == 2U);
    CHECK(strcmp(file.instructions[1].argv[0], "second-cmd") == 0);
    CHECK(strcmp(file.instructions[1].argv[1], "only-arg") == 0);
    CHECK(strcmp(file.instructions[1].stdin_path, "") == 0);
    CHECK(strcmp(file.instructions[1].stdout_path, "out2.txt") == 0);
    CHECK(strcmp(file.instructions[1].stderr_path, "") == 0);
    CHECK(strcmp(file.instructions[1].exit_path, "exit2.txt") == 0);
    CHECK(strcmp(file.instructions[1].cwd, "") == 0);
    CHECK(strcmp(file.instructions[1].env_path, "env2.txt") == 0);
    CHECK(strcmp(file.instructions[1].extra, "") == 0);

    instruction_file_free(&file);
    remove(path);
}

typedef void (*test_fn)(void);

static const test_fn tests[] = {
    test_write_file_two_records_roundtrip
};

size_t run_instruction_write_file_tests(void)
{
    size_t count = sizeof(tests) / sizeof(tests[0]);
    size_t i;

    for (i = 0U; i < count; ++i) {
        tests[i]();
    }
    return count;
}
