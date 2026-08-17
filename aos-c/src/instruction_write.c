#include "aos/instruction_write.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define INSTRUCTION_WRITE_FIELD_COUNT 7U

/* CR is rejected too because a trailing CR would be consumed as CRLF. */
static int contains_line_break(const char *text)
{
    return strchr(text, '\n') != NULL || strchr(text, '\r') != NULL;
}

/* The seven non-argv lines in serialized order. */
static void instruction_write_fields(
    const instruction_t *instruction,
    const char *fields[INSTRUCTION_WRITE_FIELD_COUNT])
{
    fields[0] = instruction->stdin_path;
    fields[1] = instruction->stdout_path;
    fields[2] = instruction->stderr_path;
    fields[3] = instruction->exit_path;
    fields[4] = instruction->cwd;
    fields[5] = instruction->env_path;
    fields[6] = instruction->extra;
}

/* Validate the complete record before either writer produces any bytes. */
static instruction_write_state instruction_write_validate(
    const instruction_t *instruction)
{
    const char *fields[INSTRUCTION_WRITE_FIELD_COUNT];
    size_t index;

    if (instruction == NULL) {
        return INSTRUCTION_WRITE_INVALID_ARGUMENT;
    }
    if (instruction->argc == 0U) {
        return INSTRUCTION_WRITE_EMPTY_ARGV;
    }
    /* argv is a borrowed pointer, so a zeroed instruction leaves it NULL. */
    if (instruction->argv == NULL) {
        return INSTRUCTION_WRITE_NULL_ARGUMENT;
    }
    if (instruction->argc > AOS_INSTRUCTION_ARGV_MAX) {
        return INSTRUCTION_WRITE_TOO_MANY_ARGUMENTS;
    }

    for (index = 0U; index < instruction->argc; ++index) {
        const char *argument = instruction->argv[index];

        if (argument == NULL) {
            return INSTRUCTION_WRITE_NULL_ARGUMENT;
        }
        if (strchr(argument, '\t') != NULL) {
            return INSTRUCTION_WRITE_ARGUMENT_CONTAINS_TAB;
        }
        if (contains_line_break(argument)) {
            return INSTRUCTION_WRITE_ARGUMENT_CONTAINS_LINE_BREAK;
        }
    }
    /*
     * A single empty argument has no tab to mark its boundary, so it
     * serializes to an empty argv line, which the reader treats as no
     * arguments at all instead of round-tripping back to argc == 1.
     */
    if (instruction->argc == 1U && instruction->argv[0][0] == '\0') {
        return INSTRUCTION_WRITE_EMPTY_ARGV;
    }

    instruction_write_fields(instruction, fields);

    for (index = 0U; index < INSTRUCTION_WRITE_FIELD_COUNT; ++index) {
        if (fields[index] == NULL) {
            return INSTRUCTION_WRITE_NULL_FIELD;
        }
        if (contains_line_break(fields[index])) {
            return INSTRUCTION_WRITE_FIELD_CONTAINS_LINE_BREAK;
        }
    }

    return INSTRUCTION_WRITE_OK;
}

/* Measure serialized bytes with overflow checks; the trailing NUL is extra. */
static instruction_write_state instruction_write_size(
    const instruction_t *instruction, size_t *serialized_size)
{
    const char *fields[INSTRUCTION_WRITE_FIELD_COUNT];
    instruction_write_state state;
    size_t total = 0U;
    size_t index;

    state = instruction_write_validate(instruction);
    if (state != INSTRUCTION_WRITE_OK) {
        return state;
    }

    instruction_write_fields(instruction, fields);

    for (index = 0U; index < instruction->argc; ++index) {
        size_t length = strlen(instruction->argv[index]);

        if (length > SIZE_MAX - total) {
            return INSTRUCTION_WRITE_SIZE_OVERFLOW;
        }
        total += length;
    }
    if (instruction->argc > SIZE_MAX - total) {
        return INSTRUCTION_WRITE_SIZE_OVERFLOW;
    }
    /* argc - 1 tabs plus the newline after argv equals argc separators. */
    total += instruction->argc;

    for (index = 0U; index < INSTRUCTION_WRITE_FIELD_COUNT; ++index) {
        size_t length = strlen(fields[index]);

        if (length >= SIZE_MAX - total) {
            return INSTRUCTION_WRITE_SIZE_OVERFLOW;
        }
        total += length + 1U;
    }

    *serialized_size = total;
    return INSTRUCTION_WRITE_OK;
}

instruction_write_state instruction_write(FILE *stream,
                                          const instruction_t *instruction)
{
    const char *fields[INSTRUCTION_WRITE_FIELD_COUNT];
    instruction_write_state state;
    size_t index;

    if (stream == NULL) {
        return INSTRUCTION_WRITE_INVALID_ARGUMENT;
    }
    state = instruction_write_validate(instruction);
    if (state != INSTRUCTION_WRITE_OK) {
        return state;
    }

    /* Line 1: Tab-separated argv followed by LF. */
    for (index = 0U; index < instruction->argc; ++index) {
        if (index > 0U && fputc('\t', stream) == EOF) {
            return INSTRUCTION_WRITE_IO_FAILED;
        }
        if (fputs(instruction->argv[index], stream) == EOF) {
            return INSTRUCTION_WRITE_IO_FAILED;
        }
    }
    if (fputc('\n', stream) == EOF) {
        return INSTRUCTION_WRITE_IO_FAILED;
    }

    instruction_write_fields(instruction, fields);

    /* Lines 2 through 8: one validated field and LF each. */
    for (index = 0U; index < INSTRUCTION_WRITE_FIELD_COUNT; ++index) {
        if (fputs(fields[index], stream) == EOF ||
            fputc('\n', stream) == EOF) {
            return INSTRUCTION_WRITE_IO_FAILED;
        }
    }

    return INSTRUCTION_WRITE_OK;
}

instruction_write_state instruction_write_buffer(
    char **ptr, size_t *remaining, const instruction_t *instruction)
{
    const char *fields[INSTRUCTION_WRITE_FIELD_COUNT];
    instruction_write_state state;
    size_t serialized_size;
    size_t index;
    char *destination;

    if (ptr == NULL || *ptr == NULL || remaining == NULL) {
        return INSTRUCTION_WRITE_INVALID_ARGUMENT;
    }

    state = instruction_write_size(instruction, &serialized_size);
    if (state != INSTRUCTION_WRITE_OK) {
        return state;
    }
    /* One additional byte is required for the parser's terminating NUL. */
    if (*remaining <= serialized_size) {
        return INSTRUCTION_WRITE_BUFFER_TOO_SMALL;
    }

    destination = *ptr;
    for (index = 0U; index < instruction->argc; ++index) {
        size_t length = strlen(instruction->argv[index]);

        if (index > 0U) {
            *destination++ = '\t';
        }
        memcpy(destination, instruction->argv[index], length);
        destination += length;
    }
    *destination++ = '\n';

    instruction_write_fields(instruction, fields);

    for (index = 0U; index < INSTRUCTION_WRITE_FIELD_COUNT; ++index) {
        size_t length = strlen(fields[index]);

        memcpy(destination, fields[index], length);
        destination += length;
        *destination++ = '\n';
    }

    /* Leave ptr ready to append another record over this terminator. */
    *destination = '\0';
    *ptr = destination;
    *remaining -= serialized_size;
    return INSTRUCTION_WRITE_OK;
}

const char *instruction_write_state_string(instruction_write_state state)
{
    switch (state) {
    case INSTRUCTION_WRITE_OK:
        return "ok";
    case INSTRUCTION_WRITE_INVALID_ARGUMENT:
        return "invalid argument";
    case INSTRUCTION_WRITE_EMPTY_ARGV:
        return "instruction has no arguments";
    case INSTRUCTION_WRITE_TOO_MANY_ARGUMENTS:
        return "instruction has too many arguments";
    case INSTRUCTION_WRITE_NULL_ARGUMENT:
        return "instruction contains a NULL argument";
    case INSTRUCTION_WRITE_ARGUMENT_CONTAINS_TAB:
        return "instruction argument contains a tab";
    case INSTRUCTION_WRITE_ARGUMENT_CONTAINS_LINE_BREAK:
        return "instruction argument contains a line break";
    case INSTRUCTION_WRITE_NULL_FIELD:
        return "instruction contains a NULL field";
    case INSTRUCTION_WRITE_FIELD_CONTAINS_LINE_BREAK:
        return "instruction field contains a line break";
    case INSTRUCTION_WRITE_SIZE_OVERFLOW:
        return "serialized instruction size overflows size_t";
    case INSTRUCTION_WRITE_BUFFER_TOO_SMALL:
        return "instruction output buffer is too small";
    case INSTRUCTION_WRITE_IO_FAILED:
        return "could not write instruction";
    default:
        return "unknown instruction write result";
    }
}
