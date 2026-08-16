#pragma once

#include <expected>
#include <optional>
#include <string>
#include <vector>

struct Call {
  std::vector<std::string> argv;
  std::optional<std::string> stdin_path, stdout_path, stderr_path, exit_path;
  std::optional<std::string> cwd, env, user;
};

std::expected<void, std::string> validate(const Call &);
