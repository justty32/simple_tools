#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "aos/instruction.h"

#include <stddef.h>
#include <string.h>

/* Argument slot count sized for the max argv plus a trailing NULL, shared
 * by every test file that calls instruction_read. */
#define SLOTS_COUNT (AOS_INSTRUCTION_ARGV_MAX + 1U)

/*
 * Fills `instr` with the given argv/argc and empty-string placeholders for
 * every other field, so each write test only has to override the fields it
 * cares about. Shared by test_instruction_write.c and
 * test_instruction_write_errors.c; `static inline` avoids both a multiple
 * definition error across translation units and an unused-function warning
 * in whichever file doesn't call it in every build configuration.
 */
static inline void fill_instruction(instruction_t *instr, const char **argv,
                                     size_t argc)
{
    memset(instr, 0, sizeof(*instr));
    instr->argc = argc;
    instr->argv = argv;
    instr->stdin_path = "";
    instr->stdout_path = "";
    instr->stderr_path = "";
    instr->exit_path = "";
    instr->cwd = "";
    instr->env_path = "";
    instr->extra = "";
}

/* Runs every instruction_read success-path test and returns how many it
 * ran. */
size_t run_instruction_read_tests(void);

/* Runs every instruction_read rejection/boundary test. */
size_t run_instruction_read_error_tests(void);

/* Runs every instruction_write / instruction_write_buffer success-path
 * test. */
size_t run_instruction_write_tests(void);

/* Runs every instruction_write / instruction_write_buffer rejection test. */
size_t run_instruction_write_error_tests(void);

/* Runs every instruction_file_t test and returns how many it ran. */
size_t run_instruction_file_tests(void);

/* Runs the total-memory-budget boundary tests (exact cap / one byte over). */
size_t run_instruction_budget_tests(void);

/* Runs the test proving non-argv Tabs never consume argv slots. */
size_t run_instruction_budget_tabs_tests(void);

/* Runs the multi-record argv_slots layout test. */
size_t run_instruction_file_slots_tests(void);

/* Runs the error_index / instruction_error diagnostics tests. */
size_t run_instruction_file_error_index_tests(void);

/* Runs the instruction_write(FILE *, ...) success-path test. */
size_t run_instruction_write_file_tests(void);

#endif
