#include "aos/inst.hpp"

#include <algorithm>
#include <istream>
#include <ostream>

namespace aos {
namespace {

/* 七個非 argv 欄位的數量。 */
constexpr std::size_t kFieldCount = kInstLineCount - 1;

/* read_line 的結果，只在這個檔案內使用。 */
enum class LineState { Ok, Eof, Incomplete, TooLong, ReadError };

/*
 * 讀入一行，並在超出剩餘預算時立刻停手。
 *
 * 這裡不用 std::getline，是因為它會把任意長度的一行整個讀進記憶體 ——
 * 預算要能擋住畸形輸入，就必須在讀取的過程中檢查，而不是讀完再檢查。
 */
LineState read_line(std::istream &in, std::string &line,
                    std::size_t budget_left)
{
    char ch;

    line.clear();
    while (in.get(ch)) {
        if (ch == '\n') {
            /* 移除 CRLF 組合中的 CR；單獨位於結尾的 CR 視為資料。 */
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return LineState::Ok;
        }
        if (line.size() == budget_left) {
            return LineState::TooLong;
        }
        line.push_back(ch);
    }

    /* badbit，或是 eofbit 以外的失敗，都代表串流出了問題而非輸入用完。 */
    if (in.bad() || !in.eof()) {
        return LineState::ReadError;
    }
    return line.empty() ? LineState::Eof : LineState::Incomplete;
}

/*
 * 依定位字元切開 argv 行。引數數量會在配置任何東西之前先數過並檢查，
 * 因此遭拒絕的行不會留下半套結果。
 */
InstState split_argv(const std::string &line, std::vector<std::string> &argv)
{
    if (line.empty()) {
        return InstState::EmptyArgv;
    }

    const std::size_t count =
        1 + static_cast<std::size_t>(std::count(line.begin(), line.end(), '\t'));
    if (count > kInstArgvMax) {
        return InstState::TooManyArgs;
    }

    argv.clear();
    argv.reserve(count);
    for (std::string::size_type start = 0;;) {
        const std::string::size_type tab = line.find('\t', start);

        if (tab == std::string::npos) {
            argv.push_back(line.substr(start));
            break;
        }
        argv.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return InstState::Ok;
}

/* CR 也會被拒絕，因為結尾的 CR 讀回來時會被當成 CRLF 的一部分吃掉。 */
bool contains_line_break(const std::string &text)
{
    return text.find('\n') != std::string::npos ||
           text.find('\r') != std::string::npos;
}

/* 在任何內容送進串流之前，先驗證整筆記錄。 */
InstState validate(const Instruction &inst)
{
    if (inst.argv.empty()) {
        return InstState::EmptyArgv;
    }
    if (inst.argv.size() > kInstArgvMax) {
        return InstState::TooManyArgs;
    }

    for (const std::string &argument : inst.argv) {
        if (argument.find('\t') != std::string::npos) {
            return InstState::ArgumentContainsTab;
        }
        if (contains_line_break(argument)) {
            return InstState::ArgumentContainsLineBreak;
        }
    }
    /*
     * 單一空引數沒有定位字元可以標示邊界，序列化後會是一行空的 argv，
     * 讀回來時會被當成完全沒有引數，而不是還原成一個引數。
     */
    if (inst.argv.size() == 1 && inst.argv[0].empty()) {
        return InstState::EmptyArgv;
    }

    const std::string *const fields[kFieldCount] = {
        &inst.stdin_path, &inst.stdout_path, &inst.stderr_path,
        &inst.exit_path,  &inst.cwd,         &inst.env_path,
        &inst.extra
    };
    for (const std::string *field : fields) {
        if (contains_line_break(*field)) {
            return InstState::FieldContainsLineBreak;
        }
    }

    return InstState::Ok;
}

}  /* namespace */

void Instruction::clear()
{
    /*
     * 每個 std::string 清空後仍保留既有容量，所以用同一個 Instruction
     * 連續讀取整條串流時，欄位的緩衝區會被重複使用。
     */
    argv.clear();
    stdin_path.clear();
    stdout_path.clear();
    stderr_path.clear();
    exit_path.clear();
    cwd.clear();
    env_path.clear();
    extra.clear();
}

InstState read_instruction(std::istream &in, Instruction &inst,
                           std::size_t max_record_bytes)
{
    if (max_record_bytes == 0) {
        return InstState::InvalidArgument;
    }

    inst.clear();

    /*
     * 第 2 到 8 行直接讀進目的欄位，省下一輪複製，也讓每個字串沿用既有
     * 容量；只有 argv 行需要暫存，因為它還得再切開。
     */
    std::string *const fields[kFieldCount] = {
        &inst.stdin_path, &inst.stdout_path, &inst.stderr_path,
        &inst.exit_path,  &inst.cwd,         &inst.env_path,
        &inst.extra
    };
    std::string argv_line;
    std::size_t used = 0;

    /* 任何失敗都要讓 inst 保持清空，半筆記錄絕不能被當成完整的一筆。 */
    auto fail = [&inst](InstState state) {
        inst.clear();
        return state;
    };

    for (std::size_t i = 0; i < kInstLineCount; ++i) {
        std::string &line = (i == 0) ? argv_line : *fields[i - 1];

        switch (read_line(in, line, max_record_bytes - used)) {
        case LineState::Ok:
            break;
        case LineState::Eof:
            /* 位於記錄之間是乾淨的結束，位於記錄內部則不是。 */
            return fail(i == 0 ? InstState::Eof : InstState::Incomplete);
        case LineState::Incomplete:
            return fail(InstState::Incomplete);
        case LineState::TooLong:
            return fail(InstState::TooLong);
        case LineState::ReadError:
            return fail(InstState::ReadError);
        }
        used += line.size();
    }

    const InstState state = split_argv(argv_line, inst.argv);
    if (state != InstState::Ok) {
        return fail(state);
    }
    return InstState::Ok;
}

InstState write_instruction(std::ostream &out, const Instruction &inst)
{
    const InstState state = validate(inst);
    if (state != InstState::Ok) {
        return state;
    }

    /* 第 1 行：以定位字元分隔的 argv，然後換行。 */
    for (std::size_t i = 0; i < inst.argv.size(); ++i) {
        if (i > 0) {
            out << '\t';
        }
        out << inst.argv[i];
    }
    out << '\n';

    const std::string *const fields[kFieldCount] = {
        &inst.stdin_path, &inst.stdout_path, &inst.stderr_path,
        &inst.exit_path,  &inst.cwd,         &inst.env_path,
        &inst.extra
    };
    /* 第 2 到 8 行：每行一個已驗證的欄位加換行。 */
    for (const std::string *field : fields) {
        out << *field << '\n';
    }

    /* ostream 的失敗狀態是黏著的，所以最後檢查一次就夠。 */
    return out ? InstState::Ok : InstState::WriteError;
}

std::size_t inst_argv_max()
{
    return kInstArgvMax;
}

std::vector<char *> to_c_argv(Instruction &inst)
{
    std::vector<char *> result;

    result.reserve(inst.argv.size() + 1);
    for (std::string &argument : inst.argv) {
        /* C++11 起 std::string 的儲存區保證連續且以 NUL 結尾。 */
        result.push_back(&argument[0]);
    }
    result.push_back(nullptr);
    return result;
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
    }
    return "unknown instruction result";
}

}  /* namespace aos */
