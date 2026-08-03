#pragma once
#include <string>

// Small filesystem / process helpers.
namespace dcap {

bool ensure_dir(const std::string& path);
bool write_file(const std::string& path, const std::string& content);
std::string read_file(const std::string& path); // "" if missing/empty
bool path_exists(const std::string& path);

// Replace every "@NAME@" in s with name.
std::string substitute_name(std::string s, const std::string& name);

// std::system wrapper; flushes stdout/stderr first so child output does not
// interleave ahead of ours. Returns the raw status (0 == success).
int run(const std::string& cmd);

} // namespace dcap
