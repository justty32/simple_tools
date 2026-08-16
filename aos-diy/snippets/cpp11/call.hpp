#pragma once

#include <string>
#include <vector>

namespace aos_diy {
namespace snippets11 {

class OptionalString {
public:
  OptionalString() : present_(false) {}
  OptionalString(const char *value) : present_(true), value_(value) {}
  OptionalString(const std::string &value) : present_(true), value_(value) {}

  bool has_value() const { return present_; }
  const std::string &value() const { return value_; }
  const std::string *operator->() const { return &value_; }
  const std::string &operator*() const { return value_; }

  void reset() {
    present_ = false;
    value_.clear();
  }

private:
  bool present_;
  std::string value_;
};

struct Call {
  std::vector<std::string> argv;
  OptionalString stdin_path;
  OptionalString stdout_path;
  OptionalString stderr_path;
  OptionalString exit_path;
  OptionalString cwd;
  OptionalString env;
  OptionalString user;
};

} // namespace snippets11
} // namespace aos_diy
