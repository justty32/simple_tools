#include "aos/inst.h"

#include <stdlib.h>
#include <string.h>

/* The seven non-argv lines, in serialized order. */
#define AOS_INST_FIELD_COUNT 7U

/* Initial storage allocation; one read grows it to fit the longest record. */
#define AOS_INST_STORAGE_MIN 256U

void aos_inst_init(aos_inst_t *inst)
{
    if (inst != NULL) {
        memset(inst, 0, sizeof(*inst));
    }
}

void aos_inst_free(aos_inst_t *inst)
{
    if (inst == NULL) {
        return;
    }
    /*
     * Both are NULL on an instruction whose fields were assigned by hand,
     * so a borrowed record passed to aos_inst_write is never freed here.
     */
    free(inst->storage);
    free(inst->argv_slots);
    memset(inst, 0, sizeof(*inst));
}

size_t aos_inst_argv_max(void)
{
    return AOS_INST_ARGV_MAX;
}

/*
 * Drop the record a previous read left behind while keeping its two
 * allocations, so a caller reading a whole stream through one instruction
 * stops allocating once the buffers have reached the size of its longest
 * record. Every public field is cleared here and set only once the record
 * has been read in full, which is what leaves inst empty on every failure.
 */
static void inst_clear(aos_inst_t *inst)
{
    inst->argc = 0U;
    inst->argv = NULL;
    inst->stdin_path = NULL;
    inst->stdout_path = NULL;
    inst->stderr_path = NULL;
    inst->exit_path = NULL;
    inst->cwd = NULL;
    inst->env_path = NULL;
    inst->extra = NULL;
}

/* Make room for `needed` storage bytes without exceeding the budget. */
static aos_inst_state storage_reserve(aos_inst_t *inst, size_t needed,
                                      size_t max_record_bytes)
{
    size_t capacity;
    char *grown;

    if (needed > max_record_bytes) {
        return AOS_INST_TOO_LONG;
    }
    if (needed <= inst->storage_capacity) {
        return AOS_INST_OK;
    }

    capacity = (inst->storage_capacity == 0U) ? AOS_INST_STORAGE_MIN
                                              : inst->storage_capacity;
    while (capacity < needed) {
        /* Clamping at the budget also keeps the doubling from overflowing. */
        if (capacity > max_record_bytes / 2U) {
            capacity = max_record_bytes;
            break;
        }
        capacity *= 2U;
    }
    if (capacity > max_record_bytes) {
        capacity = max_record_bytes;
    }

    grown = (char *)realloc(inst->storage, capacity);
    if (grown == NULL) {
        return AOS_INST_ALLOC_FAILED;
    }
    inst->storage = grown;
    inst->storage_capacity = capacity;
    return AOS_INST_OK;
}

/*
 * Append one NUL-terminated line to storage, starting at *length and
 * leaving *length just past its terminator.
 *
 * Returns AOS_INST_OK for a line that ended in a newline, AOS_INST_EOF when
 * the stream ended before the line's first byte, AOS_INST_INCOMPLETE when
 * bytes arrived but no newline did, or a failure state.
 */
static aos_inst_state read_line(FILE *stream, aos_inst_t *inst,
                                size_t *length, size_t max_record_bytes)
{
    size_t start = *length;
    size_t used = *length;
    aos_inst_state state;
    int c;

    for (;;) {
        c = getc(stream);
        if (c == EOF || c == '\n') {
            break;
        }
        state = storage_reserve(inst, used + 1U, max_record_bytes);
        if (state != AOS_INST_OK) {
            return state;
        }
        inst->storage[used++] = (char)c;
    }

    if (c == EOF) {
        if (ferror(stream)) {
            return AOS_INST_READ_ERROR;
        }
        if (used == start) {
            return AOS_INST_EOF;
        }
    }

    /* Drop the CR of a CRLF pair; a bare trailing CR is data. */
    if (c == '\n' && used > start && inst->storage[used - 1U] == '\r') {
        --used;
    }

    state = storage_reserve(inst, used + 1U, max_record_bytes);
    if (state != AOS_INST_OK) {
        return state;
    }
    inst->storage[used++] = '\0';
    *length = used;

    return (c == '\n') ? AOS_INST_OK : AOS_INST_INCOMPLETE;
}

/*
 * Split the argv line on tabs in place and point argv at the result. Both
 * limits are checked before the first NUL is written, so a rejected line is
 * never left half-split.
 */
static aos_inst_state split_argv(aos_inst_t *inst, char *line)
{
    size_t argc = 1U;
    size_t index;
    char *cursor;

    if (*line == '\0') {
        return AOS_INST_EMPTY_ARGV;
    }

    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '\t') {
            ++argc;
        }
    }
    if (argc > AOS_INST_ARGV_MAX) {
        return AOS_INST_TOO_MANY_ARGS;
    }

    if (argc + 1U > inst->argv_slots_capacity) {
        const char **grown = (const char **)realloc(
            inst->argv_slots, (argc + 1U) * sizeof(*grown));

        if (grown == NULL) {
            return AOS_INST_ALLOC_FAILED;
        }
        inst->argv_slots = grown;
        inst->argv_slots_capacity = argc + 1U;
    }

    index = 0U;
    inst->argv_slots[index++] = line;
    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '\t') {
            *cursor = '\0';
            inst->argv_slots[index++] = cursor + 1;
        }
    }
    inst->argv_slots[argc] = NULL;

    inst->argv = inst->argv_slots;
    inst->argc = argc;
    return AOS_INST_OK;
}

aos_inst_state aos_inst_read_max(FILE *stream, aos_inst_t *inst,
                                 size_t max_record_bytes)
{
    /*
     * Offsets rather than pointers: storage moves under realloc while the
     * record is being read, so the eight field positions can only be turned
     * into pointers once the last line is in. Eight size_t on the stack is
     * the whole cost of not having to measure the record up front.
     */
    size_t offsets[AOS_INST_LINE_COUNT];
    size_t length = 0U;
    size_t index;
    aos_inst_state state;

    if (stream == NULL || inst == NULL || max_record_bytes == 0U) {
        return AOS_INST_INVALID_ARGUMENT;
    }

    inst_clear(inst);

    for (index = 0U; index < AOS_INST_LINE_COUNT; ++index) {
        offsets[index] = length;
        state = read_line(stream, inst, &length, max_record_bytes);
        if (state == AOS_INST_EOF) {
            /* Between records this is a clean end; inside one it is not. */
            return (index == 0U) ? AOS_INST_EOF : AOS_INST_INCOMPLETE;
        }
        if (state != AOS_INST_OK) {
            return state;
        }
    }

    state = split_argv(inst, inst->storage + offsets[0]);
    if (state != AOS_INST_OK) {
        return state;
    }

    inst->stdin_path = inst->storage + offsets[1];
    inst->stdout_path = inst->storage + offsets[2];
    inst->stderr_path = inst->storage + offsets[3];
    inst->exit_path = inst->storage + offsets[4];
    inst->cwd = inst->storage + offsets[5];
    inst->env_path = inst->storage + offsets[6];
    inst->extra = inst->storage + offsets[7];
    return AOS_INST_OK;
}

aos_inst_state aos_inst_read(FILE *stream, aos_inst_t *inst)
{
    return aos_inst_read_max(stream, inst, AOS_INST_RECORD_MAX_BYTES);
}

/* CR is rejected too, because a trailing CR would be consumed as CRLF. */
static int contains_line_break(const char *text)
{
    return strchr(text, '\n') != NULL || strchr(text, '\r') != NULL;
}

/* The seven non-argv lines in serialized order. */
static void inst_fields(const aos_inst_t *inst,
                        const char *fields[AOS_INST_FIELD_COUNT])
{
    fields[0] = inst->stdin_path;
    fields[1] = inst->stdout_path;
    fields[2] = inst->stderr_path;
    fields[3] = inst->exit_path;
    fields[4] = inst->cwd;
    fields[5] = inst->env_path;
    fields[6] = inst->extra;
}

/* Validate the whole record before any of it reaches the stream. */
static aos_inst_state inst_validate(const aos_inst_t *inst)
{
    const char *fields[AOS_INST_FIELD_COUNT];
    size_t index;

    if (inst == NULL) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    if (inst->argc == 0U) {
        return AOS_INST_EMPTY_ARGV;
    }
    if (inst->argv == NULL) {
        return AOS_INST_NULL_ARGUMENT;
    }
    if (inst->argc > AOS_INST_ARGV_MAX) {
        return AOS_INST_TOO_MANY_ARGS;
    }

    for (index = 0U; index < inst->argc; ++index) {
        const char *argument = inst->argv[index];

        if (argument == NULL) {
            return AOS_INST_NULL_ARGUMENT;
        }
        if (strchr(argument, '\t') != NULL) {
            return AOS_INST_ARGUMENT_CONTAINS_TAB;
        }
        if (contains_line_break(argument)) {
            return AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK;
        }
    }
    /*
     * A single empty argument has no tab to mark its boundary, so it would
     * serialize to an empty argv line and read back as no arguments at all
     * rather than round-tripping to argc == 1.
     */
    if (inst->argc == 1U && inst->argv[0][0] == '\0') {
        return AOS_INST_EMPTY_ARGV;
    }

    inst_fields(inst, fields);
    for (index = 0U; index < AOS_INST_FIELD_COUNT; ++index) {
        if (fields[index] == NULL) {
            return AOS_INST_NULL_FIELD;
        }
        if (contains_line_break(fields[index])) {
            return AOS_INST_FIELD_CONTAINS_LINE_BREAK;
        }
    }

    return AOS_INST_OK;
}

aos_inst_state aos_inst_write(FILE *stream, const aos_inst_t *inst)
{
    const char *fields[AOS_INST_FIELD_COUNT];
    aos_inst_state state;
    size_t index;

    if (stream == NULL) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    state = inst_validate(inst);
    if (state != AOS_INST_OK) {
        return state;
    }

    /* Line 1: tab-separated argv, then LF. */
    for (index = 0U; index < inst->argc; ++index) {
        if (index > 0U && fputc('\t', stream) == EOF) {
            return AOS_INST_WRITE_ERROR;
        }
        if (fputs(inst->argv[index], stream) == EOF) {
            return AOS_INST_WRITE_ERROR;
        }
    }
    if (fputc('\n', stream) == EOF) {
        return AOS_INST_WRITE_ERROR;
    }

    inst_fields(inst, fields);

    /* Lines 2 through 8: one validated field and an LF each. */
    for (index = 0U; index < AOS_INST_FIELD_COUNT; ++index) {
        if (fputs(fields[index], stream) == EOF ||
            fputc('\n', stream) == EOF) {
            return AOS_INST_WRITE_ERROR;
        }
    }

    return AOS_INST_OK;
}

const char *aos_inst_state_string(aos_inst_state state)
{
    switch (state) {
    case AOS_INST_OK:
        return "ok";
    case AOS_INST_INVALID_ARGUMENT:
        return "invalid argument";
    case AOS_INST_EOF:
        return "no instruction at end of stream";
    case AOS_INST_INCOMPLETE:
        return "stream ended part-way through an instruction";
    case AOS_INST_TOO_LONG:
        return "instruction exceeds the record budget";
    case AOS_INST_READ_ERROR:
        return "could not read instruction";
    case AOS_INST_EMPTY_ARGV:
        return "instruction has no arguments";
    case AOS_INST_TOO_MANY_ARGS:
        return "instruction has too many arguments";
    case AOS_INST_ALLOC_FAILED:
        return "out of memory";
    case AOS_INST_NULL_ARGUMENT:
        return "instruction contains a NULL argument";
    case AOS_INST_NULL_FIELD:
        return "instruction contains a NULL field";
    case AOS_INST_ARGUMENT_CONTAINS_TAB:
        return "instruction argument contains a tab";
    case AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK:
        return "instruction argument contains a line break";
    case AOS_INST_FIELD_CONTAINS_LINE_BREAK:
        return "instruction field contains a line break";
    case AOS_INST_WRITE_ERROR:
        return "could not write instruction";
    default:
        return "unknown instruction result";
    }
}
