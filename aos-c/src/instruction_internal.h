#ifndef AOS_INSTRUCTION_INTERNAL_H
#define AOS_INSTRUCTION_INTERNAL_H

#include "aos/instruction.h"

#include <stddef.h>

/*
 * Measure every record in buffer without modifying it: *count receives the
 * exact instruction count and *argv_slots the exact number of argv pointer
 * slots required, including one trailing NULL per instruction.
 * On failure *error_index receives the zero-based failing record.
 */
instruction_state instruction_scan(const char *buffer, size_t *count,
                                    size_t *argv_slots, size_t *error_index);

#endif
