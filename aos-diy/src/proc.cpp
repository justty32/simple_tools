#include "proc.hpp"
#include <filesystem>

bool Proc::is_legal(const std::string &path)
{
    // check if the path is a dir
    if (path.empty())
    {
        return false;
    }
    // check if path/.aos exist
    std::filesystem::path p(path + ".aos");
    if (!std::filesystem::exists(p))
    {
        return false;
    }
    // and check if path/.aos is a dir
    if (!std::filesystem::is_directory(p))
    {
        return false;
    }
    return true;
}

void Proc::read_execs()
{
    // open path + ".aos/execs"
    // read lines
}
