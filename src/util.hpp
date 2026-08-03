#pragma once
#include <string>

// Small filesystem helpers built on std::filesystem.
namespace dcap {

// Create a directory (and parents). Returns false on error.
bool ensure_dir(const std::string& path);

// Write content to path (binary mode, no newline translation). Returns false
// on error.
bool write_file(const std::string& path, const std::string& content);

// True if path exists.
bool path_exists(const std::string& path);

} // namespace dcap
