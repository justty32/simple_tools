#include "paths.hpp"
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace dcap {

std::string external_root() {
    const char* v = std::getenv("DCAP_TEMPLATES");
    return (v && *v) ? std::string(v) : std::string();
}

std::string canonical_dir(const std::string& spec) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(spec, ec), ec);
    return ec ? spec : p.string();
}

} // namespace dcap
