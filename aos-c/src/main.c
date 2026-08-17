#include "aos/aos.h"

/* 將行程啟動邏輯與可重複使用的 aos-c 進入點分開。 */
int main(void)
{
    return aos_run();
}
