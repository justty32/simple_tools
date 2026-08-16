#pragma once

#include <string>
#include <system_error>

namespace aos_diy {
namespace snippets11 {

std::error_code publish_atomically(const std::string &destination,
                                   const std::string &contents);

} // namespace snippets11
} // namespace aos_diy
