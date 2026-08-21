#pragma once

#include <istream>
#include <optional>
#include <string>
#include <vector>

struct Exec
{
    std::vector<std::string> argv;
    std::optional<std::string> stdin_path, stdout_path, stderr_path, exit_path;
    std::optional<std::string> cwd, env;
    // return true while Exec is legal
    static bool is_legal(const Exec &);
    // read one record off the stream; nullopt = no more records
    static std::optional<Exec> read_lines(std::istream &f);
};
