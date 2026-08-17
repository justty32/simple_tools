#include "aos/instruction_file.h"
#include "instruction_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Reset fields without freeing; callers free owned pointers when necessary. */
static void instruction_file_clear(instruction_file_t *file)
{
    file->buffer = NULL;
    file->instructions = NULL;
    file->argv_slots = NULL;
    file->count = 0U;
    file->error_index = 0U;
    file->instruction_error = INSTRUCTION_OK;
}

void instruction_file_init(instruction_file_t *file)
{
    if (file != NULL) {
        instruction_file_clear(file);
    }
}

void instruction_file_free(instruction_file_t *file)
{
    if (file == NULL) {
        return;
    }

    free(file->instructions);
    free(file->argv_slots);
    free(file->buffer);
    instruction_file_clear(file);
}

/* Like instruction_file_free, but keeps error_index/instruction_error. */
static void instruction_file_discard(instruction_file_t *file)
{
    free(file->instructions);
    free(file->argv_slots);
    free(file->buffer);
    file->instructions = NULL;
    file->argv_slots = NULL;
    file->buffer = NULL;
    file->count = 0U;
}

/* Read path into file->buffer with a trailing NUL; *size excludes it. */
static instruction_file_state instruction_file_load(const char *path,
                                                     instruction_file_t *file,
                                                     size_t *size)
{
    FILE *stream;
    long file_length;

    stream = fopen(path, "rb");
    if (stream == NULL) {
        return INSTRUCTION_FILE_OPEN_FAILED;
    }

    if (fseek(stream, 0L, SEEK_END) != 0) {
        fclose(stream);
        return INSTRUCTION_FILE_SEEK_FAILED;
    }
    file_length = ftell(stream);
    if (file_length < 0L || fseek(stream, 0L, SEEK_SET) != 0) {
        fclose(stream);
        return INSTRUCTION_FILE_SEEK_FAILED;
    }
    /* size + 1 (NUL included) must fit the budget; this also guards
     * size + 1 from overflowing below. */
    if ((uintmax_t)file_length >= (uintmax_t)SIZE_MAX ||
        (uintmax_t)file_length + 1U > (uintmax_t)AOS_INSTRUCTION_FILE_MAX_BYTES) {
        fclose(stream);
        return INSTRUCTION_FILE_TOO_LARGE;
    }

    *size = (size_t)file_length;
    file->buffer = (char *)malloc(*size + 1U);
    if (file->buffer == NULL) {
        fclose(stream);
        return INSTRUCTION_FILE_ALLOCATION_FAILED;
    }

    if (*size > 0U && fread(file->buffer, 1U, *size, stream) != *size) {
        fclose(stream);
        instruction_file_free(file);
        return INSTRUCTION_FILE_READ_FAILED;
    }
    if (fclose(stream) != 0) {
        instruction_file_free(file);
        return INSTRUCTION_FILE_READ_FAILED;
    }
    /* Embedded NUL would make the in-place string parser stop early. */
    if (memchr(file->buffer, '\0', *size) != NULL) {
        instruction_file_free(file);
        return INSTRUCTION_FILE_CONTAINS_NUL;
    }
    file->buffer[*size] = '\0';
    return INSTRUCTION_FILE_OK;
}

/*
 * owned_dynamic_bytes = file_size + 1
 *     + instruction_count * sizeof(instruction_t)
 *     + argv_slot_count * sizeof(const char *)
 * must not exceed AOS_INSTRUCTION_FILE_MAX_BYTES (buffer_bytes below is
 * file_size + 1). Caller bookkeeping and buffer_bytes' own string bytes
 * are not double-counted.
 */
static int instruction_file_over_budget(size_t buffer_bytes, size_t count,
                                         size_t argv_slot_count)
{
    size_t owned = buffer_bytes;
    size_t part;

    if (count > SIZE_MAX / sizeof(instruction_t)) {
        return 1;
    }
    part = count * sizeof(instruction_t);
    if (owned > SIZE_MAX - part) {
        return 1;
    }
    owned += part;

    if (argv_slot_count > SIZE_MAX / sizeof(const char *)) {
        return 1;
    }
    part = argv_slot_count * sizeof(const char *);
    if (owned > SIZE_MAX - part) {
        return 1;
    }
    owned += part;

    return owned > (size_t)AOS_INSTRUCTION_FILE_MAX_BYTES;
}

instruction_file_state instruction_file_read(const char *path,
                                             instruction_file_t *file)
{
    instruction_file_state load_state;
    instruction_state scan_state;
    size_t size, count, argv_slot_count, scan_error_index, slots_left, index;
    const char **slots;
    char *cursor;

    if (path == NULL || file == NULL) {
        return INSTRUCTION_FILE_INVALID_ARGUMENT;
    }

    /* A read replaces any previously loaded file, including on failure. */
    instruction_file_free(file);

    load_state = instruction_file_load(path, file, &size);
    if (load_state != INSTRUCTION_FILE_OK) {
        return load_state;
    }

    scan_state = instruction_scan(file->buffer, &count, &argv_slot_count,
                                   &scan_error_index);
    if (scan_state != INSTRUCTION_OK) {
        instruction_file_state file_state = INSTRUCTION_FILE_PARSE_FAILED;

        file->error_index = scan_error_index;
        file->instruction_error = scan_state;
        if (scan_state == INSTRUCTION_TOO_FEW_LINES) {
            file_state = INSTRUCTION_FILE_INCOMPLETE;
        }
        instruction_file_discard(file);
        return file_state;
    }
    if (count == 0U) {
        return INSTRUCTION_FILE_OK;
    }

    if (instruction_file_over_budget(size + 1U, count, argv_slot_count)) {
        instruction_file_free(file);
        return INSTRUCTION_FILE_TOO_LARGE;
    }

    file->instructions = (instruction_t *)calloc(
        count, sizeof(*file->instructions));
    file->argv_slots = (const char **)calloc(
        argv_slot_count, sizeof(*file->argv_slots));
    if (file->instructions == NULL || file->argv_slots == NULL) {
        instruction_file_free(file);
        return INSTRUCTION_FILE_ALLOCATION_FAILED;
    }

    /* Each successful parse advances cursor to the next eight-line record. */
    cursor = file->buffer;
    slots = file->argv_slots;
    slots_left = argv_slot_count;
    for (index = 0U; index < count && *cursor != '\0'; ++index) {
        instruction_state state = instruction_read(
            &cursor, &file->instructions[index], slots, slots_left);

        if (state != INSTRUCTION_OK) {
            instruction_file_state file_state = INSTRUCTION_FILE_PARSE_FAILED;

            /* Keep diagnostics but discard all partially parsed payload. */
            file->error_index = index;
            file->instruction_error = state;
            if (state == INSTRUCTION_TOO_FEW_LINES) {
                file_state = INSTRUCTION_FILE_INCOMPLETE;
            }
            instruction_file_discard(file);
            return file_state;
        }
        slots += file->instructions[index].argc + 1U;
        slots_left -= file->instructions[index].argc + 1U;
        ++file->count;
    }

    /* Defensive only: instruction_scan already measured this exactly. */
    if (*cursor != '\0') {
        instruction_file_discard(file);
        file->error_index = index;
        return INSTRUCTION_FILE_PARSE_FAILED;
    }

    return INSTRUCTION_FILE_OK;
}

const char *instruction_file_state_string(instruction_file_state state)
{
    switch (state) {
    case INSTRUCTION_FILE_OK:
        return "ok";
    case INSTRUCTION_FILE_INVALID_ARGUMENT:
        return "invalid argument";
    case INSTRUCTION_FILE_OPEN_FAILED:
        return "could not open instruction file";
    case INSTRUCTION_FILE_SEEK_FAILED:
        return "could not determine instruction file size";
    case INSTRUCTION_FILE_TOO_LARGE:
        return "instruction file would exceed the configured memory budget";
    case INSTRUCTION_FILE_ALLOCATION_FAILED:
        return "could not allocate memory for instruction file";
    case INSTRUCTION_FILE_READ_FAILED:
        return "could not read instruction file";
    case INSTRUCTION_FILE_CONTAINS_NUL:
        return "instruction file contains a NUL byte";
    case INSTRUCTION_FILE_INCOMPLETE:
        return "instruction file does not contain a whole number of instructions";
    case INSTRUCTION_FILE_PARSE_FAILED:
        return "could not parse an instruction";
    default:
        return "unknown instruction file result";
    }
}
