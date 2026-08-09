#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

// Binary, byte-for-byte read/write — template files are copied verbatim, not
// text-translated, so no newline or encoding conversion may happen here.
namespace dcap {

std::expected<std::string, std::string> read_text(const std::filesystem::path& path);

// Creates any missing parent directories before writing.
std::expected<void, std::string> write_text(const std::filesystem::path& path,
                                             std::string_view content);

} // namespace dcap
