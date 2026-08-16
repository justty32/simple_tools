#pragma once

#include <unistd.h>

namespace aos_diy {
namespace snippets11 {

class UniqueFd {
public:
  UniqueFd() : fd_(-1) {}
  explicit UniqueFd(int fd) : fd_(fd) {}
  ~UniqueFd() { reset(); }

  UniqueFd(UniqueFd &&other) : fd_(other.release()) {}
  UniqueFd &operator=(UniqueFd &&other) {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }

  int release() {
    int result = fd_;
    fd_ = -1;
    return result;
  }

  void reset(int replacement = -1) {
    if (fd_ >= 0) {
      (void)::close(fd_);
    }
    fd_ = replacement;
  }

private:
  UniqueFd(const UniqueFd &);
  UniqueFd &operator=(const UniqueFd &);
  int fd_;
};

} // namespace snippets11
} // namespace aos_diy
