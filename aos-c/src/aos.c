#include "aos/aos.h"

#include <stdio.h>

/* 在逐一建構執行期模組期間，暫時使用的可執行檔行為。 */
int aos_run(void)
{
    puts("Hello from aos-c!");
    return 0;
}
