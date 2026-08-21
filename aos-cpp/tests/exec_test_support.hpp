#pragma once

#include <aos/inst.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

inline std::string read_file(const std::string &path) {
    std::ifstream input(path);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

inline void write_file(const std::string &path, const std::string &content) {
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    output << content;
}

inline std::string make_temp_dir() {
    std::string pattern = "aos_exec_test_XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    REQUIRE(mkdtemp(buffer.data()) != nullptr);
    return buffer.data();
}

inline void remove_temp_dir(const std::string &path) {
    DIR *dir = opendir(path.c_str());
    if (dir != nullptr) {
        while (dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (name != "." && name != "..") {
                std::remove((path + "/" + name).c_str());
            }
        }
        closedir(dir);
    }
    rmdir(path.c_str());
}

struct TempDir {
    TempDir() : path(make_temp_dir()) {}
    ~TempDir() { remove_temp_dir(path); }
    std::string path;
};

inline aos::inst_t command(std::initializer_list<const char *> args) {
    aos::inst_t inst;
    for (const char *arg : args) {
        inst.argv.emplace_back(arg);
    }
    return inst;
}
