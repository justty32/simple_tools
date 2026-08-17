#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "aos/inst.h"
#include "test_check.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/*
 * Both helpers are `static inline` so that including this header in a
 * translation unit that happens not to call one is neither a multiple
 * definition error nor an unused-function warning.
 */

/*
 * Return a readable stream holding text, positioned at its first byte.
 * tmpfile() keeps the reader tests off the filesystem; the stream is closed
 * with fclose() like any other.
 */
static inline FILE *stream_from(const char *text)
{
    size_t length = strlen(text);
    FILE *stream = tmpfile();

    CHECK(stream != NULL);
    if (length > 0U) {
        CHECK(fwrite(text, 1U, length, stream) == length);
    }
    rewind(stream);
    return stream;
}

/*
 * Fill inst with the given argv and an empty string for all seven other
 * fields, so a write test only has to override what it is testing. The
 * result borrows every string and owns nothing, which is exactly the
 * hand-built case aos_inst_write is meant to accept.
 */
static inline void fill_inst(aos_inst_t *inst, const char **argv, size_t argc)
{
    aos_inst_init(inst);
    inst->argc = argc;
    inst->argv = argv;
    inst->stdin_path = "";
    inst->stdout_path = "";
    inst->stderr_path = "";
    inst->exit_path = "";
    inst->cwd = "";
    inst->env_path = "";
    inst->extra = "";
}

/* Each test file exports one runner returning how many cases it ran. */

/* aos_inst_read success paths: fields, argv splitting, CRLF, reuse. */
size_t run_inst_read_tests(void);

/* aos_inst_read rejection paths: EOF, INCOMPLETE, EMPTY_ARGV, read errors. */
size_t run_inst_read_error_tests(void);

/* The record budget and the argv limit, at and either side of the boundary. */
size_t run_inst_limit_tests(void);

/* aos_inst_write success paths, including a write/read round trip. */
size_t run_inst_write_tests(void);

/* aos_inst_write validation rejections, and that they emit nothing. */
size_t run_inst_write_error_tests(void);

#endif
