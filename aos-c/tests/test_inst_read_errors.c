/*
 * aos_inst_read rejection paths: EOF, INCOMPLETE, EMPTY_ARGV, invalid
 * arguments, read errors, and aos_inst_state_string coverage.
 */
#include "test_common.h"

#include <string.h>

/* Every failure must leave the instruction empty, per the header contract. */
static void check_empty(const aos_inst_t *inst)
{
    CHECK(inst->argc == 0U);
    CHECK(inst->argv == NULL);
    CHECK(inst->stdin_path == NULL);
}

static size_t test_empty_stream_is_eof(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_EOF);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_five_lines_then_eof(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("cmd\n" "in\n" "out\n" "err\n" "ex\n");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_INCOMPLETE);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_seven_lines_then_eof(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from(
        "cmd\n" "in\n" "out\n" "err\n" "ex\n" "cwd\n" "env\n");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_INCOMPLETE);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

/* All eight lines present, but the last has no trailing newline: the
 * stream ends mid-line rather than mid-record, a distinct code path from
 * ending between lines. */
static size_t test_eighth_line_missing_newline(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from(
        "cmd\n" "in\n" "out\n" "err\n" "ex\n" "cwd\n" "env\n" "extra");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_INCOMPLETE);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_empty_argv_line(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("\nin\nout\nerr\nex\ncwd\nenv\nextra\n");

    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_EMPTY_ARGV);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

static size_t test_null_stream_is_invalid_argument(void)
{
    aos_inst_t inst;
    aos_inst_state state;

    aos_inst_init(&inst);
    state = aos_inst_read(NULL, &inst);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);
    check_empty(&inst);

    aos_inst_free(&inst);
    return 1U;
}

static size_t test_null_instruction_is_invalid_argument(void)
{
    aos_inst_state state;
    FILE *stream = stream_from("cmd\n\n\n\n\n\n\n\n");

    state = aos_inst_read(stream, NULL);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);

    fclose(stream);
    return 1U;
}

static size_t test_zero_budget_is_invalid_argument(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    FILE *stream = stream_from("cmd\n\n\n\n\n\n\n\n");

    aos_inst_init(&inst);
    state = aos_inst_read_max(stream, &inst, 0U);

    CHECK(state == AOS_INST_INVALID_ARGUMENT);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    return 1U;
}

/*
 * A stream opened for writing only cannot satisfy getc, and the C standard
 * leaves it to the implementation whether that sets the error indicator or
 * merely reports end-of-file. Where it sets the indicator, as glibc does,
 * this is the only route to AOS_INST_READ_ERROR that does not need a real
 * I/O failure. An implementation that reports plain EOF instead would
 * return AOS_INST_EOF here and fail this case; that is a portability limit
 * of the test, not of the reader.
 */
static size_t test_write_only_stream_is_read_error(void)
{
    aos_inst_t inst;
    aos_inst_state state;
    const char *path = "aos_inst_read_errors_write_only.tmp";
    FILE *stream = fopen(path, "w");

    CHECK(stream != NULL);
    aos_inst_init(&inst);
    state = aos_inst_read(stream, &inst);

    CHECK(state == AOS_INST_READ_ERROR);
    check_empty(&inst);

    aos_inst_free(&inst);
    fclose(stream);
    (void)remove(path);
    return 1U;
}

static size_t test_state_string_never_empty(void)
{
    aos_inst_state state;

    for (state = AOS_INST_OK; state <= AOS_INST_WRITE_ERROR;
         state = (aos_inst_state)((int)state + 1)) {
        const char *text = aos_inst_state_string(state);

        CHECK(text != NULL);
        CHECK(text[0] != '\0');
    }

    /* An out-of-range value must still come back with a description
     * rather than a NULL or empty string. */
    CHECK(aos_inst_state_string((aos_inst_state)9999) != NULL);
    CHECK(aos_inst_state_string((aos_inst_state)9999)[0] != '\0');

    return 1U;
}

size_t run_inst_read_error_tests(void)
{
    size_t count = 0U;

    count += test_empty_stream_is_eof();
    count += test_five_lines_then_eof();
    count += test_seven_lines_then_eof();
    count += test_eighth_line_missing_newline();
    count += test_empty_argv_line();
    count += test_null_stream_is_invalid_argument();
    count += test_null_instruction_is_invalid_argument();
    count += test_zero_budget_is_invalid_argument();
    count += test_write_only_stream_is_read_error();
    count += test_state_string_never_empty();

    return count;
}
