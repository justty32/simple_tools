#ifndef AOS_CHECK_H
#define AOS_CHECK_H
/* assert 在 NDEBUG 下會整個消失，Release 建置的測試就變成空跑還報 pass。
 * 這個檢查不會被編譯選項拿掉。 */

#include <stdio.h>

static int aos_failures;

static void aos_check_at(int passed, const char *expression, const char *file,
                         int line) {
    if (passed) {
        return;
    }
    fprintf(stderr, "%s:%d：檢查失敗：%s\n", file, line, expression);
    aos_failures += 1;
}

static int aos_report(void) {
    if (aos_failures > 0) {
        fprintf(stderr, "共 %d 項檢查失敗\n", aos_failures);
        return 1;
    }
    return 0;
}

#define AOS_CHECK(expression) \
    aos_check_at((expression) ? 1 : 0, #expression, __FILE__, __LINE__)

#endif /* AOS_CHECK_H */
