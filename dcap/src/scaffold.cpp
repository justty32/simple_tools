#include "scaffold.hpp"
#include "text_file.hpp"
#include <cstdlib>

namespace fs = std::filesystem;

namespace dcap {

NewDirectory::NewDirectory(fs::path dir) : dir_(std::move(dir)) {
    std::error_code ec;
    created_ = fs::create_directories(dir_, ec) && !ec;
}

NewDirectory::~NewDirectory() {
    if (created_ && !committed_) {
        std::error_code ec;
        fs::remove_all(dir_, ec); // best effort; nothing to report to on the way out
    }
}

ScopedCurrentPath::ScopedCurrentPath(const fs::path& dir) {
    std::error_code ec;
    previous_ = fs::current_path(ec);
    if (ec)
        return;
    fs::current_path(dir, ec);
    ok_ = !ec;
}

ScopedCurrentPath::~ScopedCurrentPath() {
    if (ok_) {
        std::error_code ec;
        fs::current_path(previous_, ec);
    }
}

std::expected<void, std::string> patch_name(const fs::path& dir, const std::string& name) {
    const fs::path path = dir / "CMakeLists.txt";
    auto text = read_text(path);
    if (!text)
        return std::unexpected(text.error());

    std::string s = std::move(*text);
    const std::string key = "@NAME@";
    for (std::size_t at = s.find(key); at != std::string::npos;
         at = s.find(key, at + name.size()))
        s.replace(at, key.size(), name);
    return write_text(path, s);
}

std::expected<void, std::string> git_init(const fs::path& dir) {
    const ScopedCurrentPath cwd(dir);
    if (!cwd.ok())
        return std::unexpected("cannot enter '" + dir.string() + "'");
    // No user-controlled text in this string: `dir` was made the cwd above,
    // so the command needs nothing but a fixed literal.
    if (std::system("git init -q") != 0)
        return std::unexpected("git init failed");
    return {};
}

} // namespace dcap
