#include "util.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace dcap {

bool ensure_dir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

bool ensure_parent_dir(const std::string& path) {
    const fs::path parent = fs::path(path).parent_path();
    return parent.empty() ? true : ensure_dir(parent.string());
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out << content;
    return out.good();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool path_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

std::string substitute_name(std::string s, const std::string& name) {
    const std::string key = "@NAME@";
    for (size_t p = s.find(key); p != std::string::npos;
         p = s.find(key, p + name.size()))
        s.replace(p, key.size(), name);
    return s;
}

int run(const std::string& cmd) {
    std::cout.flush();
    std::cerr.flush();
    return std::system(cmd.c_str());
}

} // namespace dcap
