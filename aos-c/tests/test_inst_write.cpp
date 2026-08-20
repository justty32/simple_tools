#include "test_common.hpp"

#include <sstream>
#include <string>
#include <vector>

/*
 * test_common.hpp 把 run_inst_write_tests() 宣告在全域範圍（供
 * test_main.cpp 呼叫），所以這裡引入 aos 而不是把整個檔案包進
 * namespace aos 裡。
 */
using namespace aos;

namespace {

std::size_t test_round_trip_single_record()
{
    inst_t inst = make_inst({ "prog", "arg1", "arg2" });

    inst.stdin_path = "in.txt";
    inst.stdout_path = "out.txt";
    inst.stderr_path = "err.txt";
    inst.exit_path = "exit.txt";
    inst.cwd = "/tmp/work";
    inst.env = { "PATH=/bin", "HOME=/root" };
    inst.extra = "extra data";

    std::ostringstream out;

    CHECK(write_instruction(out, inst) == InstState::Ok);

    std::istringstream in(out.str());
    inst_t read_back;

    CHECK(read_instruction(in, read_back) == InstState::Ok);
    CHECK(read_back.argv == inst.argv);
    CHECK(read_back.stdin_path == inst.stdin_path);
    CHECK(read_back.stdout_path == inst.stdout_path);
    CHECK(read_back.stderr_path == inst.stderr_path);
    CHECK(read_back.exit_path == inst.exit_path);
    CHECK(read_back.cwd == inst.cwd);
    CHECK(read_back.env == inst.env);
    CHECK(read_back.extra == inst.extra);
    return 1;
}

std::size_t test_round_trip_two_records_then_eof()
{
    inst_t first = make_inst({ "one" });
    inst_t second = make_inst({ "two", "b" });

    first.cwd = "/first";
    second.cwd = "/second";

    std::ostringstream out;

    CHECK(write_instruction(out, first) == InstState::Ok);
    CHECK(write_instruction(out, second) == InstState::Ok);

    std::istringstream in(out.str());
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv == first.argv);
    CHECK(inst.cwd == "/first");

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv == second.argv);
    CHECK(inst.cwd == "/second");

    CHECK(read_instruction(in, inst) == InstState::Eof);
    CHECK(inst.empty());
    return 1;
}

std::size_t test_exact_output_bytes()
{
    inst_t inst = make_inst({ "a", "b" });

    inst.stdin_path = "in";
    inst.stdout_path = "out";
    inst.stderr_path = "err";
    inst.exit_path = "exit";
    inst.cwd = "cwd";
    inst.env = { "E=v", "F=w" };
    inst.extra = "extra";

    std::ostringstream out;

    CHECK(write_instruction(out, inst) == InstState::Ok);

    /*
     * 逐行核對：定位字元分隔 argv、每一行欄位都以 LF 結尾，最後再多一行
     * 固定為空的分隔行。
     */
    const std::string expected =
        "a\tb\n"
        "in\n"
        "out\n"
        "err\n"
        "exit\n"
        "cwd\n"
        "E=v\tF=w\n"
        "extra\n"
        "\n";

    CHECK(out.str() == expected);
    return 1;
}

std::size_t test_all_seven_fields_survive_round_trip()
{
    inst_t inst = make_inst({ "prog" });

    inst.stdin_path = "field-1";
    inst.stdout_path = "field-2";
    inst.stderr_path = "field-3";
    inst.exit_path = "field-4";
    inst.cwd = "field-5";
    inst.env = { "FIELD=6" };
    inst.extra = "field-7";

    std::ostringstream out;

    CHECK(write_instruction(out, inst) == InstState::Ok);

    std::istringstream in(out.str());
    inst_t read_back;

    CHECK(read_instruction(in, read_back) == InstState::Ok);
    CHECK(read_back.stdin_path == "field-1");
    CHECK(read_back.stdout_path == "field-2");
    CHECK(read_back.stderr_path == "field-3");
    CHECK(read_back.exit_path == "field-4");
    CHECK(read_back.cwd == "field-5");
    CHECK(read_back.env.size() == 1);
    CHECK(read_back.env[0] == "FIELD=6");
    CHECK(read_back.extra == "field-7");
    return 1;
}

/*
 * 空的 env 寫出來是一行空的第 7 行，讀回來還是空的 —— 「繼承環境」這個
 * 意思必須能原樣往返，否則寫入端就沒辦法表達它。
 */
std::size_t test_empty_env_round_trips()
{
    inst_t inst = make_inst({ "prog" });
    std::ostringstream out;

    CHECK(write_instruction(out, inst) == InstState::Ok);
    /* argv + 六行空欄位 + 空 env + 空 extra + 空分隔行 = 九個 LF。 */
    CHECK(out.str() == "prog\n\n\n\n\n\n\n\n\n");

    std::istringstream in(out.str());
    inst_t read_back;

    CHECK(read_instruction(in, read_back) == InstState::Ok);
    CHECK(read_back.env.empty());
    return 1;
}

std::size_t test_env_values_round_trip()
{
    inst_t inst = make_inst({ "prog" });

    /* 空值、含 '=' 的值、含空白與引號的值，全都不需要跳脫。 */
    inst.env = { "EMPTY=", "PAIR=x=y", "MSG=hello world", "Q=\"quoted\"" };

    std::ostringstream out;

    CHECK(write_instruction(out, inst) == InstState::Ok);

    std::istringstream in(out.str());
    inst_t read_back;

    CHECK(read_instruction(in, read_back) == InstState::Ok);
    CHECK(read_back.env == inst.env);
    return 1;
}

std::size_t test_argument_with_spaces_and_quotes_round_trips()
{
    inst_t inst = make_inst({ "a b", "\"quoted value\"", "c" });
    std::ostringstream out;

    CHECK(write_instruction(out, inst) == InstState::Ok);

    std::istringstream in(out.str());
    inst_t read_back;

    CHECK(read_instruction(in, read_back) == InstState::Ok);
    CHECK(read_back.argv == inst.argv);
    return 1;
}

}  /* namespace */

std::size_t run_inst_write_tests()
{
    std::size_t count = 0;

    count += test_round_trip_single_record();
    count += test_round_trip_two_records_then_eof();
    count += test_exact_output_bytes();
    count += test_all_seven_fields_survive_round_trip();
    count += test_empty_env_round_trips();
    count += test_env_values_round_trip();
    count += test_argument_with_spaces_and_quotes_round_trips();
    return count;
}
