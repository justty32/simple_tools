#pragma once
#include <expected>
#include <filesystem>
#include <string>

namespace dcap {

// Creates `dir` in the constructor; removes it (recursively) in the
// destructor unless commit() was called. Never touches a directory it did
// not itself create — check ok() before relying on that guarantee.
class NewDirectory {
public:
    explicit NewDirectory(std::filesystem::path dir);
    ~NewDirectory();

    NewDirectory(const NewDirectory&) = delete;
    NewDirectory& operator=(const NewDirectory&) = delete;
    NewDirectory(NewDirectory&&) = delete;
    NewDirectory& operator=(NewDirectory&&) = delete;

    bool ok() const { return created_; }
    void commit() { committed_ = true; }

private:
    std::filesystem::path dir_;
    bool created_ = false;
    bool committed_ = false;
};

// Saves fs::current_path() in the constructor, restores it in the destructor.
class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const std::filesystem::path& dir);
    ~ScopedCurrentPath();

    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath(ScopedCurrentPath&&) = delete;
    ScopedCurrentPath& operator=(ScopedCurrentPath&&) = delete;

    bool ok() const { return ok_; }

private:
    std::filesystem::path previous_;
    bool ok_ = false;
};

// Replace every @NAME@ in the new project's CMakeLists.txt — and nothing else.
std::expected<void, std::string> patch_name(const std::filesystem::path& dir,
                                             const std::string& name);

// cd into `dir`, run the fixed literal `git init -q`, cd back — no
// user-controlled text ever reaches the shell.
std::expected<void, std::string> git_init(const std::filesystem::path& dir);

} // namespace dcap
