#include "test_common.hpp"

#include <sstream>
#include <string>
#include <vector>

/*
 * test_common.hpp 把 run_inst_limit_tests() 宣告在全域範圍（供
 * test_main.cpp 呼叫），所以這裡引入 aos 而不是把整個檔案包進
 * namespace aos 裡。
 */
using namespace aos;

namespace {

std::string join_tabs(const std::vector<std::string> &argv)
{
    std::string line;

    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i > 0) {
            line += '\t';
        }
        line += argv[i];
    }
    return line;
}

std::string build_record(const std::string &argv_line,
                         const std::vector<std::string> &fields)
{
    std::string record = argv_line + "\n";

    for (const std::string &field : fields) {
        record += field + "\n";
    }
    return record;
}

std::vector<std::string> seven(const std::string &value)
{
    return std::vector<std::string>(7, value);
}

/* n 筆各不相同的合法 env，用來湊出剛好 n 筆的 env 行。 */
std::string n_env(std::size_t n)
{
    std::string line;

    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) {
            line += '\t';
        }
        line += "K" + std::to_string(i) + "=v";
    }
    return line;
}

/* n 個各不相同、彼此可辨識的引數，用來湊出剛好 n 個 argv。 */
std::vector<std::string> n_args(std::size_t n)
{
    std::vector<std::string> argv;

    argv.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        argv.push_back("a" + std::to_string(i));
    }
    return argv;
}

std::size_t test_budget_exact_and_one_short()
{
    /*
     * 八行皆為兩個位元組長（不含換行），合計恰好 16 位元組；第 7 行還得
     * 是一筆合法的 KEY=VALUE，"m=" 剛好在兩個位元組內做到。
     * read_line 的預算檢查發生在把字元推進字串「之前」，所以剛好等於
     * 預算的行會成功，短一個位元組的預算則會在最後一行的第二個字元
     * 上被擋下。
     */
    const std::string argv_line = "ab";
    const std::vector<std::string> fields = {
        "cd", "ef", "gh", "ij", "kl", "m=", "op"
    };
    const std::string record = build_record(argv_line, fields);
    std::size_t cases = 0;

    {
        std::istringstream in(record);
        inst_t inst;

        CHECK(read_instruction(in, inst, 16) == InstState::Ok);
        CHECK(inst.argv.size() == 1);
        CHECK(inst.argv[0] == "ab");
        CHECK(inst.extra == "op");
        ++cases;
    }
    {
        std::istringstream in(record);
        inst_t inst;

        CHECK(read_instruction(in, inst, 15) == InstState::TooLong);
        CHECK(inst.empty());
        CHECK(inst.stdin_path == "");
        CHECK(inst.extra == "");
        ++cases;
    }

    return cases;
}

std::size_t test_max_argv_count()
{
    std::vector<std::string> argv = n_args(kInstArgvMax);
    std::istringstream in(build_record(join_tabs(argv), seven("")));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == kInstArgvMax);
    CHECK(inst.argv[0] == "a0");
    CHECK(inst.argv[kInstArgvMax - 1] == "a255");
    return 1;
}

std::size_t test_too_many_argv_count()
{
    std::vector<std::string> argv = n_args(kInstArgvMax + 1);
    std::istringstream in(build_record(join_tabs(argv), seven("")));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::TooManyArgs);
    CHECK(inst.empty());
    return 1;
}

std::size_t test_max_env_count()
{
    std::vector<std::string> fields = seven("");
    fields[5] = n_env(kInstEnvMax);
    std::istringstream in(build_record("x", fields));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.env.size() == kInstEnvMax);
    CHECK(inst.env[0] == "K0=v");
    CHECK(inst.env[kInstEnvMax - 1] == "K255=v");
    return 1;
}

std::size_t test_too_many_env_count()
{
    std::vector<std::string> fields = seven("");
    fields[5] = n_env(kInstEnvMax + 1);
    std::istringstream in(build_record("x", fields));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::TooManyEnv);
    CHECK(inst.empty());
    CHECK(inst.env.empty());
    return 1;
}

std::size_t test_inst_limits_match_constants()
{
    CHECK(inst_argv_max() == kInstArgvMax);
    CHECK(inst_argv_max() == 256);
    CHECK(inst_env_max() == kInstEnvMax);
    CHECK(inst_env_max() == 256);
    return 1;
}

std::size_t test_tabs_in_other_lines_are_ordinary()
{
    /*
     * 定位字元有切分意義的只有 argv 與 env 兩行；其餘六行是不透明資料，
     * 定位字元在裡面就只是一個普通位元組。
     */
    std::vector<std::string> fields = {
        "a\tb", "c\td\te", "f", "g\t", "\th", "", "k\t\tl"
    };
    std::istringstream in(build_record("solo", fields));
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == 1);
    CHECK(inst.stdin_path == "a\tb");
    CHECK(inst.stdout_path == "c\td\te");
    CHECK(inst.stderr_path == "f");
    CHECK(inst.exit_path == "g\t");
    CHECK(inst.cwd == "\th");
    CHECK(inst.env.empty());
    CHECK(inst.extra == "k\t\tl");
    return 1;
}

std::size_t test_reuse_across_very_different_lengths()
{
    const std::string long_field(500, 'x');
    std::vector<std::string> argv_long = n_args(50);
    std::vector<std::string> fields_long(7, long_field);
    std::vector<std::string> argv_short = { "s" };
    std::vector<std::string> fields_short = seven("");
    std::vector<std::string> argv_long2 = n_args(30);
    std::vector<std::string> fields_long2(7, long_field + "y");

    /* env 行不能沿用其他欄位的填充值，它得是合法的 KEY=VALUE。 */
    fields_long[5] = n_env(50);
    fields_long2[5] = n_env(30);

    std::string data = build_record(join_tabs(argv_long), fields_long) +
                       build_record(join_tabs(argv_short), fields_short) +
                       build_record(join_tabs(argv_long2), fields_long2);
    std::istringstream in(data);
    inst_t inst;

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == 50);
    CHECK(inst.env.size() == 50);
    CHECK(inst.stdin_path == long_field);
    CHECK(inst.extra == long_field);

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == 1);
    CHECK(inst.argv[0] == "s");
    CHECK(inst.env.empty());
    CHECK(inst.stdin_path == "");
    CHECK(inst.extra == "");

    CHECK(read_instruction(in, inst) == InstState::Ok);
    CHECK(inst.argv.size() == 30);
    CHECK(inst.env.size() == 30);
    CHECK(inst.stdin_path == long_field + "y");
    CHECK(inst.extra == long_field + "y");

    CHECK(read_instruction(in, inst) == InstState::Eof);
    CHECK(inst.empty());
    return 1;
}

}  /* namespace */

std::size_t run_inst_limit_tests()
{
    std::size_t count = 0;

    count += test_budget_exact_and_one_short();
    count += test_max_argv_count();
    count += test_too_many_argv_count();
    count += test_max_env_count();
    count += test_too_many_env_count();
    count += test_inst_limits_match_constants();
    count += test_tabs_in_other_lines_are_ordinary();
    count += test_reuse_across_very_different_lengths();
    return count;
}
