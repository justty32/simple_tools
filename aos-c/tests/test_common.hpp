#ifndef TEST_COMMON_HPP
#define TEST_COMMON_HPP

#include "aos/inst.hpp"
#include "test_check.hpp"

#include <cstddef>
#include <string>
#include <vector>

/*
 * 用給定的 argv 組出一筆指令，其餘七個欄位留空，讓每個寫入測試只需要覆寫
 * 它真正要測的欄位。
 */
inline aos::Instruction make_inst(const std::vector<std::string> &argv)
{
    aos::Instruction inst;

    inst.argv = argv;
    return inst;
}

/* 每個測試檔案匯出一個回傳執行案例數的函式。 */

/* read_instruction 的成功路徑：欄位、argv 切分、CRLF、重複使用。 */
std::size_t run_inst_read_tests();

/* read_instruction 的拒絕路徑：EOF、Incomplete、EmptyArgv、串流錯誤。 */
std::size_t run_inst_read_error_tests();

/* 記錄預算與 argv 上限，在邊界及其兩側。 */
std::size_t run_inst_limit_tests();

/* write_instruction 的成功路徑，含一次寫入後讀回的來回驗證。 */
std::size_t run_inst_write_tests();

/* write_instruction 的驗證拒絕，以及拒絕時不得寫出任何位元組。 */
std::size_t run_inst_write_error_tests();

/* execute() 的行為；非 POSIX 平台上回傳 0。 */
std::size_t run_exec_tests();

#endif
