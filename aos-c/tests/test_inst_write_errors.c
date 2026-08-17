/*
 * aos_inst_write validation rejections. Every rejection must also leave
 * the stream untouched, since the header promises the whole record is
 * validated before the first byte is written.
 */
#include "test_common.h"

#include <stdlib.h>
#include <string.h>

/* Runs one write attempt on a fresh tmpfile and checks both the returned
 * state and that nothing was written to the stream. */
static void check_rejected(const aos_inst_t *inst, aos_inst_state expected)
{
    FILE *stream = tmpfile();
    aos_inst_state state;

    CHECK(stream != NULL);
    state = aos_inst_write(stream, inst);

    CHECK(state == expected);
    CHECK(ftell(stream) == 0L);

    fclose(stream);
}

static size_t test_empty_argc_is_empty_argv(void)
{
    aos_inst_t inst;

    fill_inst(&inst, NULL, 0U);
    check_rejected(&inst, AOS_INST_EMPTY_ARGV);
    return 1U;
}

static size_t test_null_argv_with_positive_argc(void)
{
    aos_inst_t inst;

    fill_inst(&inst, NULL, 1U);
    check_rejected(&inst, AOS_INST_NULL_ARGUMENT);
    return 1U;
}

/*
 * argc alone decides this: inst_validate rejects on the count before it
 * ever indexes into argv, so the array need not actually hold that many
 * live entries for the check to be exercised safely.
 */
static size_t test_argc_over_max_is_too_many_args(void)
{
    const char *argv[AOS_INST_ARGV_MAX + 1U];
    aos_inst_t inst;
    size_t i;

    for (i = 0U; i < AOS_INST_ARGV_MAX + 1U; ++i) {
        argv[i] = "a";
    }
    fill_inst(&inst, argv, AOS_INST_ARGV_MAX + 1U);
    check_rejected(&inst, AOS_INST_TOO_MANY_ARGS);
    return 1U;
}

static size_t test_null_entry_inside_argv(void)
{
    const char *argv[] = { "a", NULL };
    aos_inst_t inst;

    fill_inst(&inst, argv, 2U);
    check_rejected(&inst, AOS_INST_NULL_ARGUMENT);
    return 1U;
}

static size_t test_argument_contains_tab(void)
{
    const char *argv[] = { "has\ttab" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    check_rejected(&inst, AOS_INST_ARGUMENT_CONTAINS_TAB);
    return 1U;
}

static size_t test_argument_contains_newline(void)
{
    const char *argv[] = { "has\nbreak" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    check_rejected(&inst, AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK);
    return 1U;
}

static size_t test_argument_contains_cr(void)
{
    const char *argv[] = { "has\rbreak" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    check_rejected(&inst, AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK);
    return 1U;
}

/*
 * A lone empty argument has no tab to mark a boundary, so it cannot
 * round-trip: it would serialize to an empty argv line and read back as
 * AOS_INST_EMPTY_ARGV rather than argc == 1. The writer rejects it up
 * front instead of producing an unreadable record.
 */
static size_t test_single_empty_argument_is_empty_argv(void)
{
    const char *argv[] = { "" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    check_rejected(&inst, AOS_INST_EMPTY_ARGV);
    return 1U;
}

/* One case per non-argv field, built by resetting a fresh borrowed
 * instruction each time and nulling exactly one field. */
static size_t test_null_field_each_of_seven(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;
    size_t field;

    for (field = 0U; field < 7U; ++field) {
        fill_inst(&inst, argv, 1U);
        switch (field) {
        case 0U: inst.stdin_path = NULL; break;
        case 1U: inst.stdout_path = NULL; break;
        case 2U: inst.stderr_path = NULL; break;
        case 3U: inst.exit_path = NULL; break;
        case 4U: inst.cwd = NULL; break;
        case 5U: inst.env_path = NULL; break;
        default: inst.extra = NULL; break;
        }
        check_rejected(&inst, AOS_INST_NULL_FIELD);
    }
    return 1U;
}

static size_t test_field_contains_newline(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    inst.cwd = "has\nbreak";
    check_rejected(&inst, AOS_INST_FIELD_CONTAINS_LINE_BREAK);
    return 1U;
}

static size_t test_field_contains_cr(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    inst.extra = "has\rbreak";
    check_rejected(&inst, AOS_INST_FIELD_CONTAINS_LINE_BREAK);
    return 1U;
}

static size_t test_null_stream_is_invalid_argument(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;
    aos_inst_state state;

    fill_inst(&inst, argv, 1U);
    state = aos_inst_write(NULL, &inst);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);
    return 1U;
}

static size_t test_null_instruction_is_invalid_argument(void)
{
    FILE *stream = tmpfile();
    aos_inst_state state;

    CHECK(stream != NULL);
    state = aos_inst_write(stream, NULL);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);
    CHECK(ftell(stream) == 0L);

    fclose(stream);
    return 1U;
}

/*
 * A hand-built instruction owns none of its storage: aos_inst_free must
 * free nothing (clean under ASan) and still zero the struct.
 */
static size_t test_free_on_borrowed_instruction_is_clean(void)
{
    const char *argv[] = { "cmd" };
    aos_inst_t inst;

    fill_inst(&inst, argv, 1U);
    aos_inst_free(&inst);

    CHECK(inst.argc == 0U);
    CHECK(inst.argv == NULL);
    CHECK(inst.stdin_path == NULL);
    CHECK(inst.stdout_path == NULL);
    CHECK(inst.stderr_path == NULL);
    CHECK(inst.exit_path == NULL);
    CHECK(inst.cwd == NULL);
    CHECK(inst.env_path == NULL);
    CHECK(inst.extra == NULL);
    CHECK(inst.storage == NULL);
    CHECK(inst.storage_capacity == 0U);
    CHECK(inst.argv_slots == NULL);
    CHECK(inst.argv_slots_capacity == 0U);

    return 1U;
}

size_t run_inst_write_error_tests(void)
{
    size_t count = 0U;

    count += test_empty_argc_is_empty_argv();
    count += test_null_argv_with_positive_argc();
    count += test_argc_over_max_is_too_many_args();
    count += test_null_entry_inside_argv();
    count += test_argument_contains_tab();
    count += test_argument_contains_newline();
    count += test_argument_contains_cr();
    count += test_single_empty_argument_is_empty_argv();
    count += test_null_field_each_of_seven();
    count += test_field_contains_newline();
    count += test_field_contains_cr();
    count += test_null_stream_is_invalid_argument();
    count += test_null_instruction_is_invalid_argument();
    count += test_free_on_borrowed_instruction_is_clean();

    return count;
}
