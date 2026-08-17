#ifndef AOS_INSTRUCTION_FILE_H
#define AOS_INSTRUCTION_FILE_H

#include "aos/instruction.h"

#include <stddef.h>

/*
 * Upper bound on the total dynamic memory this module will own for one
 * file: the input buffer plus the exact instructions and argv_slots
 * arrays sized to hold it (see instruction_file_read's budget check for
 * the precise formula). Define this macro at compile time to select a
 * different limit.
 */
#ifndef AOS_INSTRUCTION_FILE_MAX_BYTES
#define AOS_INSTRUCTION_FILE_MAX_BYTES (64U * 1024U * 1024U)
#endif

/* Result of loading and parsing a complete instruction file. */
typedef enum instruction_file_state {
    INSTRUCTION_FILE_OK = 0,
    INSTRUCTION_FILE_INVALID_ARGUMENT,
    INSTRUCTION_FILE_OPEN_FAILED,
    INSTRUCTION_FILE_SEEK_FAILED,
    INSTRUCTION_FILE_TOO_LARGE,
    INSTRUCTION_FILE_ALLOCATION_FAILED,
    INSTRUCTION_FILE_READ_FAILED,
    INSTRUCTION_FILE_CONTAINS_NUL,
    INSTRUCTION_FILE_INCOMPLETE,
    INSTRUCTION_FILE_PARSE_FAILED
} instruction_file_state;

/* Owner of a whole-file buffer and the zero-copy views parsed from it. */
typedef struct instruction_file {
    /* Owned storage backing every string in instructions. */
    char *buffer;
    /* Owned array containing count successfully parsed instructions. */
    instruction_t *instructions;
    /* Owned argv slot storage that every instructions[i].argv borrows. */
    const char **argv_slots;
    size_t count;
    /*
     * Zero-based failing instruction and its parser error. These fields are
     * meaningful for INSTRUCTION_FILE_INCOMPLETE and
     * INSTRUCTION_FILE_PARSE_FAILED.
     */
    size_t error_index;
    instruction_state instruction_error;
} instruction_file_t;

/* Initialize an instruction_file_t before its first read. */
void instruction_file_init(instruction_file_t *file);

/*
 * Read and parse every eight-line instruction in path.
 *
 * The file must be initialized before its first read; passing an
 * uninitialized instruction_file_t is undefined behavior, because this
 * function's first action is to call instruction_file_free() on it. Calling
 * read again is supported and first releases the previous buffer and
 * instruction array. An empty input file succeeds with count equal to zero.
 */
instruction_file_state instruction_file_read(const char *path,
                                             instruction_file_t *file);

/* Release owned memory and return every field to its initialized value. */
void instruction_file_free(instruction_file_t *file);

/* Return a static, human-readable description of state. */
const char *instruction_file_state_string(instruction_file_state state);

#endif
