#include "aos/inst.hpp"

#include <string>
#include <vector>

/*
 * The instruction type itself: clearing it, reporting the limits compiled
 * into this library, handing its storage to execv, and naming its states.
 *
 * Nothing here knows what a record looks like on the wire. That is
 * inst_format.cpp, which is where read_instruction and write_instruction
 * live.
 */
namespace aos {

void inst_t::clear()
{
    /*
     * 每個 std::string 清空後仍保留既有容量，所以用同一個 inst_t
     * 連續讀取整條串流時，欄位的緩衝區會被重複使用。
     */
    argv.clear();
    stdin_path.clear();
    stdout_path.clear();
    stderr_path.clear();
    exit_path.clear();
    cwd.clear();
    env.clear();
    extra.clear();
}

std::size_t inst_argv_max()
{
    return kInstArgvMax;
}

std::size_t inst_env_max()
{
    return kInstEnvMax;
}

namespace {

/* argv 與 env 攤成 char ** 的做法完全相同，差別只在來源。 */
std::vector<char *> borrow(std::vector<std::string> &values)
{
    std::vector<char *> result;

    result.reserve(values.size() + 1);
    for (std::string &value : values) {
        /* C++11 起 std::string 的儲存區保證連續且以 NUL 結尾。 */
        result.push_back(&value[0]);
    }
    result.push_back(nullptr);
    return result;
}

}  /* namespace */

std::vector<char *> to_c_argv(inst_t &inst)
{
    return borrow(inst.argv);
}

std::vector<char *> to_c_envp(inst_t &inst)
{
    return borrow(inst.env);
}

const char *to_string(InstState state)
{
    switch (state) {
    case InstState::Ok:
        return "ok";
    case InstState::InvalidArgument:
        return "invalid argument";
    case InstState::Eof:
        return "no instruction at end of stream";
    case InstState::Incomplete:
        return "stream ended part-way through an instruction";
    case InstState::TooLong:
        return "instruction exceeds the record budget";
    case InstState::ReadError:
        return "could not read instruction";
    case InstState::EmptyArgv:
        return "instruction has no arguments";
    case InstState::TooManyArgs:
        return "instruction has too many arguments";
    case InstState::ArgumentContainsTab:
        return "instruction argument contains a tab";
    case InstState::ArgumentContainsLineBreak:
        return "instruction argument contains a line break";
    case InstState::FieldContainsLineBreak:
        return "instruction field contains a line break";
    case InstState::WriteError:
        return "could not write instruction";
    case InstState::EnvEntryMalformed:
        return "environment entry is not KEY=VALUE";
    case InstState::TooManyEnv:
        return "instruction has too many environment entries";
    }
    return "unknown instruction result";
}

}  /* namespace aos */
