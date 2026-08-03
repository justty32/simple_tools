#include "util.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace dcap {

bool ensure_dir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

bool write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out << content;
    return out.good();
}

bool path_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

} // namespace dcap
