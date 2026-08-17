#ifndef AOS_INSTRUCTION_WRITE_H
#define AOS_INSTRUCTION_WRITE_H

#include "aos/instruction.h"

#include <stddef.h>
#include <stdio.h>

/* Result of validating or serializing one instruction. */
typedef enum instruction_write_state {
    INSTRUCTION_WRITE_OK = 0,
    INSTRUCTION_WRITE_INVALID_ARGUMENT,
    INSTRUCTION_WRITE_EMPTY_ARGV,
    INSTRUCTION_WRITE_TOO_MANY_ARGUMENTS,
    INSTRUCTION_WRITE_NULL_ARGUMENT,
    INSTRUCTION_WRITE_ARGUMENT_CONTAINS_TAB,
    INSTRUCTION_WRITE_ARGUMENT_CONTAINS_LINE_BREAK,
    INSTRUCTION_WRITE_NULL_FIELD,
    INSTRUCTION_WRITE_FIELD_CONTAINS_LINE_BREAK,
    INSTRUCTION_WRITE_SIZE_OVERFLOW,
    INSTRUCTION_WRITE_BUFFER_TOO_SMALL,
    INSTRUCTION_WRITE_IO_FAILED
} instruction_write_state;

/*
 * Write one instruction as eight lines to stream.
 *
 * Arguments are joined with tabs. The eighth line also ends with a newline,
 * so repeated calls append records in the format consumed by
 * instruction_read() and instruction_file_read(). Validation completes before
 * the first byte is written. An I/O failure may still leave a partial record.
 * The caller retains ownership of both stream and instruction.
 */
instruction_write_state instruction_write(FILE *stream,
                                          const instruction_t *instruction);

/*
 * Write one instruction into a caller-owned buffer.
 *
 * *remaining includes the byte at **ptr and must leave room for the trailing
 * NUL. On success, *ptr points at that NUL and *remaining is reduced by the
 * serialized byte count. On validation or capacity failure, neither value nor
 * the destination buffer is changed.
 *
 * The destination must not overlap any string referenced by instruction.
 */
instruction_write_state instruction_write_buffer(
    char **ptr, size_t *remaining, const instruction_t *instruction);

/* Return a static, human-readable description of state. */
const char *instruction_write_state_string(instruction_write_state state);

#endif
