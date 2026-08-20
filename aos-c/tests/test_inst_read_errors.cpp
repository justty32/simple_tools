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

/* 讀取失敗後，inst 必須完全清空：欄位皆為空，argv 與 env 也是空的。 */
void expect_cleared(const inst_t &inst)
{
    CHECK(inst.empty());
    CHECK(inst.argv.empty());
    CHECK(inst.stdin_path == "");
    CHECK(inst.stdout_path == "");
    CHECK(inst.stderr_path == "");
    CHECK(inst.exit_path == "");
    CHECK(inst.cwd == "");
    CHECK(inst.env.empty());
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
    /*
     * argv 行本身是空字串（只有換行字元），其餘七行欄位齊全，第九行是空的
     * 分隔 —— 記錄本身結構完整，錯的只有空 argv，所以會走到 split_argv 並
     * 回報 EmptyArgv，而不是先在分隔檢查上停下。
     */
    std::string data = "\n" + n_full_lines(7) + "\n";
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

/*
 * 八行欄位齊全，但第九行不是空的（這裡直接接下一筆的 argv）→ 錯位，
 * 回報 MissingSeparator。
 */
std::size_t test_nonempty_separator_is_missing_separator()
{
    /* 八行合法欄位（argv "x" + 七行空欄位），第九行放非空內容。 */
    std::string data = "x\n\n\n\n\n\n\n\n" + std::string("nextprog\n");
    std::istringstream in(data);
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::MissingSeparator);
    expect_cleared(inst);
    return 1;
}

/*
 * 少一行的檔案：第一筆只有七行欄位，第二筆緊接其後。讀第一筆時，八行欄位的
 * 迴圈會多吃掉第二筆的第一行，於是原本該落在分隔位置的，是第二筆的第二行
 * 資料 —— 非空 → MissingSeparator。錯位一定會被偵測到；依被頂上來的是哪一行，
 * 確切狀態可能是 MissingSeparator 或更早的 EnvEntryMalformed，兩者都可接受。
 */
std::size_t test_record_short_one_line_is_detected()
{
    /*
     * 第一筆刻意只寫七行欄位（少了 extra 行），第二筆是一筆結構完整的九行
     * 記錄。讀第一筆時，第二筆的 argv 行("prog2")被當成第八欄，接著第二筆的
     * stdin 行("fa")落在分隔位置且非空。
     */
    std::string first = "prog\na\nb\nc\nd\nK=v\n";     /* 七行欄位，缺第八行 */
    std::string second = "prog2\nfa\nfb\nfc\nfd\nfe\nH=w\ngg\n\n";
    std::istringstream in(first + second);
    inst_t inst;

    const InstState state = read_instruction(in, inst);

    CHECK(state == InstState::MissingSeparator ||
          state == InstState::EnvEntryMalformed);
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
        InstState::WriteError,
        InstState::EnvEntryMalformed,
        InstState::TooManyEnv,
        InstState::MissingSeparator
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
    count += test_nonempty_separator_is_missing_separator();
    count += test_record_short_one_line_is_detected();
    count += test_to_string_covers_every_state();
    return count;
}
