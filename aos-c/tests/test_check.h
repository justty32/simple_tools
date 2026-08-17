#ifndef TEST_CHECK_H
#define TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/*
 * 發行版本會定義 -DNDEBUG，使 assert() 消失；依賴它的測試套件可能完全沒有
 * 進行檢查卻仍顯示通過。CHECK() 的定義中沒有任何 NDEBUG 防護，因此無論
 * 前置處理器的狀態為何，都不會在編譯時被移除。
 */
#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            test_check_fail(__FILE__, __LINE__, #expr); \
        } \
    } while (0)

/* 回報失敗的運算式並停止執行；只顯示第一個失敗。 */
static inline void test_check_fail(const char *file, int line,
                                    const char *expr)
{
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    exit(1);
}

#endif
