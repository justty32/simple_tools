#include "test_common.hpp"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

/*
 * test_common.hpp 把 run_inst_read_error_tests() 宣告在全域範圍（供
 * test_main.cpp 呼叫），所以這裡引入 aos 而不是把整個檔案包進
 * namespace aos 裡。
 */
using namespace aos;

namespace {

/* n 行皆以 LF 結尾的內容，每行文字都不相同，方便辨認是哪一行。 */
std::string n_full_lines(std::size_t n)
{
    std::string data;

    for (std::size_t i = 0; i < n; ++i) {
        data += "line" + std::to_string(i) + "\n";
    }
    return data;
}

/* 讀取失敗後，inst 必須完全清空：七個欄位皆為空字串，argv 也是空的。 */
void expect_cleared(const inst_t &inst)
{
    CHECK(inst.empty());
    CHECK(inst.argv.empty());
    CHECK(inst.stdin_path == "");
    CHECK(inst.stdout_path == "");
    CHECK(inst.stderr_path == "");
    CHECK(inst.exit_path == "");
    CHECK(inst.cwd == "");
    CHECK(inst.env_path == "");
    CHECK(inst.extra == "");
}

std::size_t test_empty_stream_is_eof()
{
    std::istringstream in("");
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Eof);
    expect_cleared(inst);
    return 1;
}

std::size_t test_five_lines_then_eof()
{
    std::istringstream in(n_full_lines(5));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Incomplete);
    expect_cleared(inst);
    return 1;
}

std::size_t test_eight_lines_missing_final_newline()
{
    /* 前七行完整，第八行有內容但沒有結尾換行，串流就在其後結束。 */
    std::string data = n_full_lines(7) + "last";
    std::istringstream in(data);
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Incomplete);
    expect_cleared(inst);
    return 1;
}

std::size_t test_seven_lines_all_terminated()
{
    std::istringstream in(n_full_lines(7));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Incomplete);
    expect_cleared(inst);
    return 1;
}

std::size_t test_empty_argv_line()
{
    /* argv 行本身是空字串（只有換行字元），其餘七行內容齊全。 */
    std::string data = "\n" + n_full_lines(7);
    std::istringstream in(data);
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::EmptyArgv);
    expect_cleared(inst);
    return 1;
}

std::size_t test_zero_budget_is_invalid_argument()
{
    std::istringstream in("x\n\n\n\n\n\n\n\n");
    inst_t inst;

    CHECK(read_instruction(in, inst, 0) == InstState::InvalidArgument);
    expect_cleared(inst);
    return 1;
}

std::size_t test_broken_stream_is_read_error()
{
    /*
     * 開啟一個不存在的路徑，串流在任何讀取之前就已經處於失敗狀態：
     * failbit 被設起，eofbit 卻沒有，這正是 read_line 判定 ReadError
     * 而非 Eof 的條件（in.bad() || !in.eof()）。
     */
    std::ifstream in("/nonexistent/aos-c-test-path/does-not-exist-12345");
    inst_t inst;

    CHECK(!in);
    CHECK(read_instruction(in, inst) == InstState::ReadError);
    expect_cleared(inst);
    return 1;
}

std::size_t test_to_string_covers_every_state()
{
    const InstState states[] = {
        InstState::Ok,
        InstState::InvalidArgument,
        InstState::Eof,
        InstState::Incomplete,
        InstState::TooLong,
        InstState::ReadError,
        InstState::EmptyArgv,
        InstState::TooManyArgs,
        InstState::ArgumentContainsTab,
        InstState::ArgumentContainsLineBreak,
        InstState::FieldContainsLineBreak,
        InstState::WriteError
    };
    std::size_t cases = 0;

    for (InstState state : states) {
        const char *text = to_string(state);

        CHECK(text != nullptr);
        CHECK(std::strlen(text) > 0);
        ++cases;
    }

    /* 範圍外的值走的是 switch 之外的預設回傳路徑。 */
    const char *unknown = to_string(static_cast<InstState>(9999));

    CHECK(unknown != nullptr);
    CHECK(std::strlen(unknown) > 0);
    ++cases;

    return cases;
}

}  /* namespace */

std::size_t run_inst_read_error_tests()
{
    std::size_t count = 0;

    count += test_empty_stream_is_eof();
    count += test_five_lines_then_eof();
    count += test_eight_lines_missing_final_newline();
    count += test_seven_lines_all_terminated();
    count += test_empty_argv_line();
    count += test_zero_budget_is_invalid_argument();
    count += test_broken_stream_is_read_error();
    count += test_to_string_covers_every_state();
    return count;
}
