#ifndef TEST_CHECK_HPP
#define TEST_CHECK_HPP

#include <cstdio>
#include <cstdlib>

/*
 * assert() 在 -DNDEBUG（release 版本會定義）下會消失，建立在它之上的測試
 * 套件可能什麼都沒檢查就顯示通過。CHECK() 的定義中完全沒有 NDEBUG 判斷，
 * 因此無論前置處理器的狀態為何都不會被編譯掉。
 */
#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            test_check_fail(__FILE__, __LINE__, #expr); \
        } \
    } while (0)

/* 回報失敗的運算式並中止執行；以第一個失敗為準。 */
inline void test_check_fail(const char *file, int line, const char *expr)
{
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    std::exit(1);
}

#endif
