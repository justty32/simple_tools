#pragma once

#include <optional>
#include <string>
#include <vector>

namespace aos_diy::snippets {

// C++20-only copy of the boundary shape.  Keep validation separate so this
// header does not inherit the project's C++23 std::expected dependency.
struct Call {
  std::vector<std::string> argv;
  std::optional<std::string> stdin_path;
  std::optional<std::string> stdout_path;
  std::optional<std::string> stderr_path;
  std::optional<std::string> exit_path;
  std::optional<std::string> cwd;
  std::optional<std::string> env;
  std::optional<std::string> user;
};

} // namespace aos_diy::snippets
