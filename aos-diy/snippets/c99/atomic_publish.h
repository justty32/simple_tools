#ifndef AOS_DIY_C99_ATOMIC_PUBLISH_H
#define AOS_DIY_C99_ATOMIC_PUBLISH_H

#include <stddef.h>

/* Returns 0 on success, otherwise an errno value. */
int aos_publish_atomically(const char *destination, const void *contents,
                           size_t size);

#endif
