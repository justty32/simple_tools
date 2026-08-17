#ifndef TEST_CHECK_H
#define TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/*
 * assert() disappears under -DNDEBUG, which the release build defines; a
 * test suite built on it can go green without checking anything. CHECK()
 * has no NDEBUG guard anywhere in its definition, so it cannot be compiled
 * out regardless of preprocessor state.
 */
#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            test_check_fail(__FILE__, __LINE__, #expr); \
        } \
    } while (0)

/* Reports the failing expression and stops the run; first failure wins. */
static inline void test_check_fail(const char *file, int line,
                                    const char *expr)
{
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    exit(1);
}

#endif
