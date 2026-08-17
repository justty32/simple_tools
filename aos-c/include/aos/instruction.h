#ifndef AOS_INSTRUCTION_H
#define AOS_INSTRUCTION_H

#include <stddef.h>

/*
 * Maximum number of arguments a single instruction may carry, excluding the
 * trailing NULL pointer. Shared by the reader and the writer so that every
 * record the writer accepts is also one the reader can parse back.
 */
#define AOS_INSTRUCTION_ARGV_MAX 256U

/*
 * Number of lines in one serialized instruction record. Part of the
 * on-disk format contract shared by the reader, the scanner, and the
 * writer; changing it changes the format.
 */
#define AOS_INSTRUCTION_LINE_COUNT 8U

/* Result of decoding one eight-line instruction. */
typedef enum instruction_state {
    INSTRUCTION_OK = 0,
    INSTRUCTION_INVALID_ARGUMENT,
    INSTRUCTION_TOO_FEW_LINES,
    INSTRUCTION_EMPTY_ARGV,
    INSTRUCTION_TOO_MANY_ARGUMENTS
} instruction_state;

/* A zero-copy view of one serialized instruction. */
typedef struct instruction {
    size_t argc;
    /*
     * Borrowed slot array supplied by the caller of instruction_read;
     * argv[argc] is always NULL after a successful parse. The caller must
     * keep the slots alive as long as this instruction is in use.
     */
    const char **argv;
    /* Lines 2 through 6 are required paths; empty strings remain empty. */
    const char *stdin_path;
    const char *stdout_path;
    const char *stderr_path;
    const char *exit_path;
    const char *cwd;
    /* Lines 7 and 8 are opaque to the parser and may be empty. */
    const char *env_path;
    const char *extra;
} instruction_t;

/*
 * Parse eight lines in this order:
 * argv, stdin, stdout, stderr, exit, cwd, env, extra.
 *
 * The argv line contains tab-separated arguments. No quoting or escaping is
 * performed; spaces, quotes, and backslashes are ordinary characters. An
 * empty argv line is invalid. Adjacent tabs preserve empty arguments.
 *
 * argv_slots is caller-provided storage for the parsed argument pointers,
 * borrowed the same way the input buffer is: it must stay alive and
 * unchanged while instruction is in use. A single-instruction caller can
 * declare it on the stack as
 *     const char *slots[AOS_INSTRUCTION_ARGV_MAX + 1U];
 * Parsing fails with INSTRUCTION_TOO_MANY_ARGUMENTS once argc would reach
 * AOS_INSTRUCTION_ARGV_MAX or argc + 1 would exceed argv_slots_count,
 * whichever comes first.
 *
 * Parsing is performed in place: line endings and argv separators are
 * replaced with NUL bytes. Every pointer stored in instruction therefore
 * borrows from the input buffer. Keep the buffer alive and unchanged while
 * using the result.
 *
 * On success, *ptr advances to the next instruction or to the terminating
 * NUL byte. Every one of the eight lines, including the last, must end with
 * a newline; this lets a truncated record be told apart from a complete one
 * whose eighth line happens to be empty. Both LF and CRLF line endings are
 * accepted.
 */
instruction_state instruction_read(char **ptr,
                                    instruction_t *instruction,
                                    const char **argv_slots,
                                    size_t argv_slots_count);

/* Return a static, human-readable description of state. */
const char *instruction_state_string(instruction_state state);

#endif
