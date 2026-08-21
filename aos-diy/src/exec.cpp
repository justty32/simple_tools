#include "exec.hpp"
#include <sstream>

namespace
{

bool check_argv(const std::vector<std::string> &argv)
{
    if (argv.empty())
    {
        return false;
    }
    if (argv[0].empty())
    {
        return false;
    }
    for (std::size_t i = 0; i < argv.size(); ++i)
    {
        if (argv[i].find('\0') != std::string::npos)
        {
            return false;
        }
    }
    return true;
}

std::vector<std::string> split_argv(const std::string &line)
{
    std::istringstream in(line);
    std::vector<std::string> argv;
    for (std::string word; in >> word;)
    {
        argv.push_back(word);
    }
    return argv;
}

} // namespace

bool Exec::is_legal(const Exec &call)
{
    return check_argv(call.argv);
}

// first line is argv, split by space
// 2th~7th line is stdin/out/err, exit status, cwd, env
// a field line of "-" means that field is absent
// lines starting with "#" are ignored, an empty line ends the record
std::optional<Exec> Exec::read_lines(std::istream &f)
{
    Exec exec;
    // 第 2~7 行照順序落在這六個欄位上
    std::optional<std::string> *const fields[6] = {&exec.stdin_path, &exec.stdout_path, &exec.stderr_path,
                                                   &exec.exit_path, &exec.cwd,         &exec.env};
    std::size_t filled = 0; // 已經吃進去的行數：0 = 連 argv 都還沒讀到，7 = 讀滿了

    for (std::string line; std::getline(f, line);)
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (!line.empty() && line.front() == '#')
        {
            continue;
        }
        if (line.empty())
        {
            if (filled == 0)
            {
                continue; // 還沒開始，這是兩筆之間的空行
            }
            return exec; // 空行 = 這一筆到此為止，後面的欄位留 nullopt
        }

        if (filled == 0)
        {
            exec.argv = split_argv(line);
        }
        else if (line != "-") // "-" = 這個欄位不給，位置照佔，值留 nullopt
        {
            *fields[filled - 1] = line;
        }

        if (++filled == 7)
        {
            return exec;
        }
    }

    if (filled == 0)
    {
        return std::nullopt; // 串流讀完了，沒有下一筆
    }
    return exec; // EOF 同樣是這一筆的結束
}
