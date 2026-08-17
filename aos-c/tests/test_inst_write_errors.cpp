#include "test_common.hpp"

#include <sstream>
#include <string>
#include <vector>

/*
 * test_common.hpp 把 run_inst_write_error_tests() 宣告在全域範圍（供
 * test_main.cpp 呼叫），所以這裡引入 aos 而不是把整個檔案包進
 * namespace aos 裡。
 */
using namespace aos;

namespace {

std::vector<std::string> n_args(std::size_t n)
{
    std::vector<std::string> argv;

    argv.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        argv.push_back("a" + std::to_string(i));
    }
    return argv;
}

/* 每個拒絕案例都必須確認：狀態碼正確，而且沒有任何位元組寫出去。 */
std::size_t check_rejected(const Instruction &inst, InstState expected)
{
    std::ostringstream out;

    CHECK(write_instruction(out, inst) == expected);
    CHECK(out.str().empty());
    return 1;
}

std::size_t test_empty_argv()
{
    Instruction inst;

    return check_rejected(inst, InstState::EmptyArgv);
}

std::size_t test_too_many_args()
{
    Instruction inst = make_inst(n_args(kInstArgvMax + 1));

    return check_rejected(inst, InstState::TooManyArgs);
}

std::size_t test_argument_contains_tab()
{
    Instruction inst = make_inst({ "ok", "has\ttab" });

    return check_rejected(inst, InstState::ArgumentContainsTab);
}

std::size_t test_argument_contains_line_break()
{
    std::size_t cases = 0;

    {
        Instruction inst = make_inst({ "ok", "has\nnewline" });

        cases += check_rejected(inst, InstState::ArgumentContainsLineBreak);
    }
    {
        Instruction inst = make_inst({ "ok", "has\rcr" });

        cases += check_rejected(inst, InstState::ArgumentContainsLineBreak);
    }

    return cases;
}

std::size_t test_single_empty_argument()
{
    /*
     * 唯一的引數是空字串：沒有定位字元可以標示它的存在，序列化後會變成
     * 一行空白，讀回來會被誤判成完全沒有引數，因此在寫入端就先拒絕。
     */
    Instruction inst = make_inst({ "" });

    return check_rejected(inst, InstState::EmptyArgv);
}

std::size_t test_field_contains_line_break()
{
    std::string Instruction::* const fields[] = {
        &Instruction::stdin_path, &Instruction::stdout_path,
        &Instruction::stderr_path, &Instruction::exit_path,
        &Instruction::cwd, &Instruction::env_path, &Instruction::extra
    };
    const char breaks[] = { '\n', '\r' };
    std::size_t cases = 0;

    for (std::string Instruction::* field : fields) {
        for (char brk : breaks) {
            Instruction inst = make_inst({ "prog" });

            inst.*field = std::string("value") + brk;
            cases += check_rejected(inst, InstState::FieldContainsLineBreak);
        }
    }

    return cases;
}

std::size_t test_failed_ostream_is_write_error()
{
    Instruction inst = make_inst({ "prog", "arg" });
    std::ostringstream out;

    /* 串流在任何插入動作之前就已失敗，所以插入運算子全部是無效動作。 */
    out.setstate(std::ios::failbit);

    CHECK(write_instruction(out, inst) == InstState::WriteError);
    return 1;
}

}  /* namespace */

std::size_t run_inst_write_error_tests()
{
    std::size_t count = 0;

    count += test_empty_argv();
    count += test_too_many_args();
    count += test_argument_contains_tab();
    count += test_argument_contains_line_break();
    count += test_single_empty_argument();
    count += test_field_contains_line_break();
    count += test_failed_ostream_is_write_error();
    return count;
}
