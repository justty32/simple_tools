/*
 * aos-c 測試套件的進入點。每個測試檔案都會匯出一個 run_*_tests() 函式
 * （宣告於 test_common.hpp），用來執行其中的測試案例並回傳執行數量；
 * 此檔案只會依序呼叫所有函式，並印出合計數量。
 */
#include "test_common.hpp"

#include <iostream>

int main()
{
    std::size_t count = 0;

    count += run_inst_read_tests();
    count += run_inst_read_error_tests();
    count += run_inst_limit_tests();
    count += run_inst_write_tests();
    count += run_inst_write_error_tests();
    count += run_exec_tests();

    std::cout << count << " tests passed\n";
    return 0;
}
