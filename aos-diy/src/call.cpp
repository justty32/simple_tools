#include "call.hpp"

namespace {

std::expected<void, std::string> check_argv(const std::vector<std::string> &argv) {
  if (argv.empty()) {
    return std::unexpected("argv: must not be empty");
  }
  if (argv[0].empty()) {
    return std::unexpected("argv[0]: must not be empty");
  }
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (argv[i].find('\0') != std::string::npos) {
      return std::unexpected("argv[" + std::to_string(i) + "]: must not contain NUL");
    }
  }
  return {};
}

} // namespace

std::expected<void, std::string> validate(const Call &call) {
  return check_argv(call.argv);
}
