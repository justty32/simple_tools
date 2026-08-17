#ifndef AOS_INST_H
#define AOS_INST_H

#include <stddef.h>
#include <stdio.h>

/*
 * One instruction: a serialized process spawn, and the reader and writer
 * that move it between a stream and memory.
 *
 * An instruction is eight lines, one per field:
 *   argv, stdin, stdout, stderr, exit, cwd, env, extra.
 * The argv line is tab-separated with no quoting or escaping; spaces,
 * quotes and backslashes are ordinary characters, and adjacent tabs
 * preserve empty arguments. Every line, including the eighth, must end with
 * a newline, which is what lets a truncated record be told apart from a
 * complete one whose eighth line happens to be empty. Both LF and CRLF are
 * accepted on input; the writer always emits LF.
 *
 * Reading is stream-based: aos_inst_read takes a FILE * and consumes
 * exactly one record, so pipes and stdin work and memory is bounded by the
 * longest record rather than by the size of the input. Random access across
 * every record in a file is what that trades away.
 */

/*
 * Maximum number of arguments one instruction may carry, excluding the
 * trailing NULL. Shared by the reader and the writer, so every record the
 * writer accepts is one the reader can parse back. Call aos_inst_argv_max()
 * rather than reading this macro when the value has to match the one
 * compiled into the library.
 */
#define AOS_INST_ARGV_MAX 256U

/* Number of lines in one serialized record; changing it changes the format. */
#define AOS_INST_LINE_COUNT 8U

/*
 * Default per-record byte budget for aos_inst_read. It bounds the storage a
 * single record may claim, counting the eight lines and the NUL that
 * terminates each of them. It is a default, not a limit on the API: any
 * positive budget may be passed to aos_inst_read_max.
 */
#define AOS_INST_RECORD_MAX_BYTES (1024U * 1024U)

/*
 * Result of reading, or of validating and writing, one instruction.
 *
 * Consumers compile against the numeric values, so new states may only be
 * appended; reordering silently changes the meaning of a stored code.
 */
typedef enum aos_inst_state {
    AOS_INST_OK = 0,
    AOS_INST_INVALID_ARGUMENT,
    /* Read: no record began before the end of the stream. */
    AOS_INST_EOF,
    /* Read: the stream ended part-way through a record. */
    AOS_INST_INCOMPLETE,
    /* Read: the record exceeded the caller's byte budget. */
    AOS_INST_TOO_LONG,
    /* Read: the stream reported an error. */
    AOS_INST_READ_ERROR,
    /* Read or write: the argv line holds no arguments. */
    AOS_INST_EMPTY_ARGV,
    /* Read or write: the argv line holds more than AOS_INST_ARGV_MAX. */
    AOS_INST_TOO_MANY_ARGS,
    AOS_INST_ALLOC_FAILED,
    /* Write: an argv entry or a field pointer was NULL. */
    AOS_INST_NULL_ARGUMENT,
    AOS_INST_NULL_FIELD,
    /* Write: a value holds a byte the format uses as a separator. */
    AOS_INST_ARGUMENT_CONTAINS_TAB,
    AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK,
    AOS_INST_FIELD_CONTAINS_LINE_BREAK,
    /* Write: the stream reported an error, possibly mid-record. */
    AOS_INST_WRITE_ERROR
} aos_inst_state;

/*
 * One instruction.
 *
 * After a successful aos_inst_read every string points into storage owned
 * by this struct and stays valid until the next read on it or until
 * aos_inst_free. A caller may also fill the public fields by hand to hand a
 * borrowed record to aos_inst_write; the private fields stay zero in that
 * case and aos_inst_free then frees nothing, so borrowed strings are never
 * at risk.
 */
typedef struct aos_inst {
    size_t argc;
    /* argv[argc] is always NULL after a successful read. */
    const char **argv;
    /* Lines 2 through 6 are paths; an empty line stays an empty string. */
    const char *stdin_path;
    const char *stdout_path;
    const char *stderr_path;
    const char *exit_path;
    const char *cwd;
    /* Lines 7 and 8 are opaque to the parser and may be empty. */
    const char *env_path;
    const char *extra;

    /*
     * Private to the reader. storage backs every string above and
     * argv_slots backs argv, which is only ever a view onto one of them.
     * The capacities let successive reads on the same instruction reuse
     * both allocations. Treat all four as opaque: only aos_inst_read and
     * aos_inst_free may change them.
     */
    char *storage;
    size_t storage_capacity;
    const char **argv_slots;
    size_t argv_slots_capacity;
} aos_inst_t;

/*
 * Zero an instruction. Required before its first aos_inst_read, and the way
 * to prepare one for aos_inst_write before assigning the public fields.
 */
void aos_inst_init(aos_inst_t *inst);

/*
 * Release whatever the instruction owns and return it to its initialized
 * state. Safe on NULL, on an already-freed instruction, and on one whose
 * fields were assigned by hand, which owns nothing and so frees nothing.
 */
void aos_inst_free(aos_inst_t *inst);

/*
 * Read the next record from stream into inst, with a budget of
 * AOS_INST_RECORD_MAX_BYTES.
 *
 * inst must have been initialized; this function's first act is to discard
 * any record already in it. Returns AOS_INST_EOF when the stream ended
 * cleanly between records, leaving inst empty. On every failure inst is
 * left empty as well, and the bytes consumed so far are gone: a stream
 * cannot be rewound, so AOS_INST_INCOMPLETE and AOS_INST_TOO_LONG end the
 * run rather than inviting a retry.
 */
aos_inst_state aos_inst_read(FILE *stream, aos_inst_t *inst);

/*
 * Read the next record with an explicit per-record byte budget, which must
 * be positive. A record needing more than max_record_bytes of storage fails
 * with AOS_INST_TOO_LONG instead of allocating without bound; an
 * instruction file is a trust boundary, so a budget always applies.
 */
aos_inst_state aos_inst_read_max(FILE *stream, aos_inst_t *inst,
                                 size_t max_record_bytes);

/*
 * Write one instruction to stream as eight lines, each ending in LF, so
 * repeated calls append records that aos_inst_read consumes.
 *
 * The whole record is validated before the first byte is written, so a
 * rejected instruction leaves the stream untouched. A write that fails
 * part-way through may still have emitted a partial record. The caller
 * keeps ownership of both stream and instruction.
 */
aos_inst_state aos_inst_write(FILE *stream, const aos_inst_t *inst);

/* The argv limit compiled into this library. */
size_t aos_inst_argv_max(void);

/* Return a static, human-readable description of state. */
const char *aos_inst_state_string(aos_inst_state state);

#endif
