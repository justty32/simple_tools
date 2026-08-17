#include "aos/instruction.h"
#include "instruction_internal.h"

#include <stdint.h>
#include <string.h>

/* Restore the public view to a safe empty state before parsing into it. */
static void instruction_clear(instruction_t *instruction,
                               const char **argv_slots)
{
    memset(instruction, 0, sizeof(*instruction));
    instruction->argv = argv_slots;
}

/* Locate one line's terminator; *length excludes the LF and a CRLF's CR. */
static const char *line_bound(const char *cursor, size_t *length)
{
    const char *newline = strchr(cursor, '\n');
    size_t len;

    if (newline == NULL) {
        return NULL;
    }
    len = (size_t)(newline - cursor);
    if (len > 0U && cursor[len - 1U] == '\r') {
        --len;
    }
    *length = len;
    return newline;
}

/* Split exactly one eight-line record and leave cursor at the next record. */
static instruction_state split_lines(char **cursor,
                                      char *lines[AOS_INSTRUCTION_LINE_COUNT])
{
    size_t lines_i;

    for (lines_i = 0U; lines_i < AOS_INSTRUCTION_LINE_COUNT; ++lines_i) {
        const char *newline;
        size_t length;

        lines[lines_i] = *cursor;
        newline = line_bound(*cursor, &length);
        if (newline == NULL) {
            return INSTRUCTION_TOO_FEW_LINES;
        }
        /* length already excludes CR, so this NUL lands on the CR of a
         * CRLF pair or, absent one, on the LF itself. */
        (*cursor)[length] = '\0';
        *cursor = (char *)newline + 1;
    }

    return INSTRUCTION_OK;
}

/* Replace Tab separators with NUL and build the borrowed argv pointer array. */
static instruction_state split_argv(char *line, instruction_t *instruction,
                                     size_t argv_slots_count)
{
    char *cursor = line;

    if (*cursor == '\0') {
        instruction->argv[0] = NULL;
        return INSTRUCTION_EMPTY_ARGV;
    }

    for (;;) {
        char *tab;

        /*
         * A slot is needed both for this entry and, later, for the
         * trailing NULL, so argc + 1 reaching argv_slots_count leaves no
         * room for one more entry.
         */
        if (instruction->argc == AOS_INSTRUCTION_ARGV_MAX ||
            instruction->argc + 1U == argv_slots_count) {
            return INSTRUCTION_TOO_MANY_ARGUMENTS;
        }

        /* argv entries borrow slices of the first instruction line. */
        instruction->argv[instruction->argc++] = cursor;
        tab = strchr(cursor, '\t');
        if (tab == NULL) {
            break;
        }

        *tab = '\0';
        cursor = tab + 1;
    }

    instruction->argv[instruction->argc] = NULL;
    return INSTRUCTION_OK;
}

instruction_state instruction_read(char **ptr,
                                    instruction_t *instruction,
                                    const char **argv_slots,
                                    size_t argv_slots_count)
{
    char *lines[AOS_INSTRUCTION_LINE_COUNT];
    instruction_state state;

    if (ptr == NULL || instruction == NULL || *ptr == NULL ||
        argv_slots == NULL || argv_slots_count == 0U) {
        return INSTRUCTION_INVALID_ARGUMENT;
    }

    /* No result from an earlier call may survive a failed parse. */
    instruction_clear(instruction, argv_slots);
    state = split_lines(ptr, lines);
    if (state != INSTRUCTION_OK) {
        return state;
    }

    state = split_argv(lines[0], instruction, argv_slots_count);
    if (state != INSTRUCTION_OK) {
        return state;
    }

    instruction->stdin_path = lines[1];
    instruction->stdout_path = lines[2];
    instruction->stderr_path = lines[3];
    instruction->exit_path = lines[4];
    instruction->cwd = lines[5];
    instruction->env_path = lines[6];
    instruction->extra = lines[7];
    return INSTRUCTION_OK;
}

instruction_state instruction_scan(const char *buffer, size_t *count,
                                    size_t *argv_slots, size_t *error_index)
{
    const char *cursor = buffer;
    size_t total_count = 0U;
    size_t total_slots = 0U;

    for (;;) {
        size_t line_i;
        size_t argc = 0U;

        if (*cursor == '\0') {
            break;
        }

        for (line_i = 0U; line_i < AOS_INSTRUCTION_LINE_COUNT; ++line_i) {
            const char *newline;
            size_t length;

            newline = line_bound(cursor, &length);
            if (newline == NULL) {
                *error_index = total_count;
                return INSTRUCTION_TOO_FEW_LINES;
            }

            /* Only the argv line's Tabs delimit arguments; the other
             * seven lines are opaque payload and must not be scanned. */
            if (line_i == 0U) {
                const char *scan;

                if (length == 0U) {
                    *error_index = total_count;
                    return INSTRUCTION_EMPTY_ARGV;
                }
                argc = 1U;
                for (scan = cursor; scan < cursor + length; ++scan) {
                    if (*scan == '\t') {
                        if (argc == AOS_INSTRUCTION_ARGV_MAX) {
                            *error_index = total_count;
                            return INSTRUCTION_TOO_MANY_ARGUMENTS;
                        }
                        ++argc;
                    }
                }
            }

            cursor = newline + 1;
        }

        /* argc <= AOS_INSTRUCTION_ARGV_MAX, so argc + 1 cannot overflow;
         * only the running total needs an overflow check. */
        if (total_slots > SIZE_MAX - (argc + 1U)) {
            *error_index = total_count;
            return INSTRUCTION_TOO_MANY_ARGUMENTS;
        }
        total_slots += argc + 1U;

        if (total_count == SIZE_MAX) {
            *error_index = total_count;
            return INSTRUCTION_TOO_MANY_ARGUMENTS;
        }
        ++total_count;
    }

    *count = total_count;
    *argv_slots = total_slots;
    return INSTRUCTION_OK;
}

const char *instruction_state_string(instruction_state state)
{
    switch (state) {
    case INSTRUCTION_OK:
        return "ok";
    case INSTRUCTION_INVALID_ARGUMENT:
        return "invalid argument";
    case INSTRUCTION_TOO_FEW_LINES:
        return "instruction has fewer than eight lines";
    case INSTRUCTION_EMPTY_ARGV:
        return "instruction has no arguments";
    case INSTRUCTION_TOO_MANY_ARGUMENTS:
        return "instruction has too many arguments";
    default:
        return "unknown instruction result";
    }
}
