/*
 * Entry point for the aos-c test suite. Each test file exports one
 * run_*_tests() function (declared in test_common.h) that runs its cases
 * and returns how many it ran; this file just calls all of them in order
 * and prints the combined total.
 */
#include "test_common.h"

#include <stdio.h>

int main(void)
{
    size_t count = 0U;

    count += run_instruction_read_tests();
    count += run_instruction_read_error_tests();
    count += run_instruction_write_tests();
    count += run_instruction_write_error_tests();
    count += run_instruction_file_tests();
    count += run_instruction_budget_tests();
    count += run_instruction_budget_tabs_tests();
    count += run_instruction_file_slots_tests();
    count += run_instruction_file_error_index_tests();
    count += run_instruction_write_file_tests();

    printf("%zu instruction tests passed\n", count);
    return 0;
}
