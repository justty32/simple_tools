#include "aos/inst.hpp"

#include <algorithm>
#include <istream>
#include <ostream>

/*
 * The eight-line encoding, and nothing else: this file is the only place
 * that knows a record is eight lines, that tabs separate argv and env, or
 * that a line ends in LF. inst.cpp owns the type itself and never looks at
 * a byte of it.
 *
 * The split is by subject rather than by size. Everything here answers "how
 * does a record turn into bytes and back", and that question is the one
 * that changes when the format changes -- the type, its limits and its
 * adapters do not move with it.
 */
namespace aos {
namespace {

/* 除了 argv 與 env 之外，其餘六行都是單純的字串欄位。 */
constexpr std::size_t kStringFieldCount = 6;

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

/* 一行裡的 token 數；空行也算一個（空的）token。 */
std::size_t token_count(const std::string &line)
{
    const std::ptrdiff_t tabs = std::count(line.begin(), line.end(), '\t');

    return 1 + static_cast<std::size_t>(tabs);
}

/* 依定位字元切開一行。空 token 會保留，因為它們在格式裡是有意義的資料。 */
void split_tabs(const std::string &line, std::vector<std::string> &out)
{
    out.clear();
    out.reserve(token_count(line));
    for (std::string::size_type start = 0;;) {
        const std::string::size_type tab = line.find('\t', start);

        if (tab == std::string::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

/*
 * 切開 argv 行。引數數量會在配置任何東西之前先數過並檢查，因此遭拒絕的行
 * 不會留下半套結果。
 */
InstState split_argv(const std::string &line, std::vector<std::string> &argv)
{
    if (line.empty()) {
        return InstState::EmptyArgv;
    }
    if (token_count(line) > kInstArgvMax) {
        return InstState::TooManyArgs;
    }
    split_tabs(line, argv);
    return InstState::Ok;
}

/*
 * 一筆 env 必須看起來像 KEY=VALUE：要有 '='，而且它不能在最前面。值可以是
 * 空的，也可以自己含 '='，只有第一個 '=' 是分隔符號。
 */
bool valid_env_entry(const std::string &entry)
{
    const std::string::size_type eq = entry.find('=');

    return eq != std::string::npos && eq != 0;
}

/*
 * 切開 env 行。空行是空的清單，不是錯誤 —— 這是它與 argv 行唯一的差別，
 * 也是「繼承呼叫端環境」在格式裡的寫法。
 */
InstState split_env(const std::string &line, std::vector<std::string> &env)
{
    env.clear();
    if (line.empty()) {
        return InstState::Ok;
    }
    if (token_count(line) > kInstEnvMax) {
        return InstState::TooManyEnv;
    }
    split_tabs(line, env);
    for (const std::string &entry : env) {
        if (!valid_env_entry(entry)) {
            return InstState::EnvEntryMalformed;
        }
    }
    return InstState::Ok;
}

/* 以定位字元接回一行並換行。空清單就是一行空的。 */
void write_tabbed(std::ostream &out, const std::vector<std::string> &values)
{
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << '\t';
        }
        out << values[i];
    }
    out << '\n';
}

/* CR 也會被拒絕，因為結尾的 CR 讀回來時會被當成 CRLF 的一部分吃掉。 */
bool contains_line_break(const std::string &text)
{
    return text.find('\n') != std::string::npos ||
           text.find('\r') != std::string::npos;
}

/* 在任何內容送進串流之前，先驗證整筆記錄。 */
InstState validate(const inst_t &inst)
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

    if (inst.env.size() > kInstEnvMax) {
        return InstState::TooManyEnv;
    }
    for (const std::string &entry : inst.env) {
        /*
         * 定位字元與換行都會讓這一筆讀不回來，跟「不是 KEY=VALUE」一樣是
         * 寫不出去的記錄，所以共用同一個狀態。
         */
        if (entry.find('\t') != std::string::npos ||
            contains_line_break(entry) || !valid_env_entry(entry)) {
            return InstState::EnvEntryMalformed;
        }
    }

    const std::string *const fields[kStringFieldCount] = {
        &inst.stdin_path, &inst.stdout_path, &inst.stderr_path,
        &inst.exit_path,  &inst.cwd,         &inst.extra
    };
    for (const std::string *field : fields) {
        if (contains_line_break(*field)) {
            return InstState::FieldContainsLineBreak;
        }
    }

    return InstState::Ok;
}

}  /* namespace */

InstState read_instruction(std::istream &in, inst_t &inst,
                           std::size_t max_record_bytes)
{
    if (max_record_bytes == 0) {
        return InstState::InvalidArgument;
    }

    inst.clear();

    /*
     * 單純的字串欄位直接讀進目的地，省下一輪複製，也讓每個字串沿用既有
     * 容量；argv 與 env 兩行需要暫存，因為它們還得再切開。
     */
    std::string argv_line;
    std::string env_line;
    std::string *const lines[kInstLineCount] = {
        &argv_line,        &inst.stdin_path, &inst.stdout_path,
        &inst.stderr_path, &inst.exit_path,  &inst.cwd,
        &env_line,         &inst.extra
    };
    std::size_t used = 0;

    /* 任何失敗都要讓 inst 保持清空，半筆記錄絕不能被當成完整的一筆。 */
    auto fail = [&inst](InstState state) {
        inst.clear();
        return state;
    };

    for (std::size_t i = 0; i < kInstLineCount; ++i) {
        std::string &line = *lines[i];

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

    const InstState argv_state = split_argv(argv_line, inst.argv);
    if (argv_state != InstState::Ok) {
        return fail(argv_state);
    }

    const InstState env_state = split_env(env_line, inst.env);
    if (env_state != InstState::Ok) {
        return fail(env_state);
    }
    return InstState::Ok;
}

InstState write_instruction(std::ostream &out, const inst_t &inst)
{
    const InstState state = validate(inst);
    if (state != InstState::Ok) {
        return state;
    }

    /* 第 1 行：以定位字元分隔的 argv。 */
    write_tabbed(out, inst.argv);

    /* 第 2 到 6 行：每行一個已驗證的路徑欄位。 */
    const std::string *const paths[kStringFieldCount - 1] = {
        &inst.stdin_path, &inst.stdout_path, &inst.stderr_path,
        &inst.exit_path,  &inst.cwd
    };
    for (const std::string *field : paths) {
        out << *field << '\n';
    }

    /* 第 7 行：以定位字元分隔的 env，空清單就是一行空的。 */
    write_tabbed(out, inst.env);
    /* 第 8 行。 */
    out << inst.extra << '\n';

    /* ostream 的失敗狀態是黏著的，所以最後檢查一次就夠。 */
    return out ? InstState::Ok : InstState::WriteError;
}


}  /* namespace aos */
