/*
 * aos-c 測試套件的進入點。每個測試檔案都會匯出一個 run_*_tests() 函式
 * （宣告於 test_common.h），用來執行其中的測試案例並回傳執行數量；
 * 此檔案只會依序呼叫所有函式，並印出合計數量。
 */
#include "test_common.h"

#include <stdio.h>

int main(void)
{
    size_t count = 0U;

    count += run_inst_read_tests();
    count += run_inst_read_error_tests();
    count += run_inst_limit_tests();
    count += run_inst_write_tests();
    count += run_inst_write_error_tests();

    printf("%zu instruction tests passed\n", count);
    return 0;
}
