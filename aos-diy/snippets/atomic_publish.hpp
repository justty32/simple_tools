#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

namespace aos_diy::snippets {

// Publishes a complete file in one rename and requests durability for both the
// file contents and its directory entry.  An empty error_code means success.
std::error_code publish_atomically(const std::filesystem::path &destination,
                                   std::string_view contents);

} // namespace aos_diy::snippets
