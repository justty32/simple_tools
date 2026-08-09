#include "text_file.hpp"
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace dcap {

std::expected<std::string, std::string> read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::unexpected("cannot open '" + path.string() + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad())
        return std::unexpected("failed reading '" + path.string() + "'");
    return ss.str();
}

std::expected<void, std::string> write_text(const fs::path& path, std::string_view content) {
    std::error_code ec;
    if (const fs::path parent = path.parent_path(); !parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec)
            return std::unexpected("cannot create '" + parent.string() + "': " + ec.message());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return std::unexpected("cannot open '" + path.string() + "' for writing");
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good())
        return std::unexpected("failed writing '" + path.string() + "'");
    return {};
}

} // namespace dcap
