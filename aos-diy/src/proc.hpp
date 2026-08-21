#pragma once

#include "exec.hpp"
#include <list>
#include <string>

struct Proc
{
    std::string path;
    // return true while the path is a legal proc dir
    static bool is_legal(const std::string &path);
    std::list<Exec> execs;
    void read_execs();
};
