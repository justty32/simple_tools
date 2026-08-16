#include "atomic_publish.hpp"

#include "unique_fd.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace aos_diy::snippets {
namespace {

std::error_code errno_code() {
  return {errno, std::generic_category()};
}

class TemporaryPath final {
public:
  explicit TemporaryPath(std::string path) : path_(std::move(path)) {}
  ~TemporaryPath() {
    if (!path_.empty()) {
      ::unlink(path_.c_str());
    }
  }

  TemporaryPath(const TemporaryPath &) = delete;
  TemporaryPath &operator=(const TemporaryPath &) = delete;

  [[nodiscard]] const char *c_str() const noexcept { return path_.c_str(); }
  void release() noexcept { path_.clear(); }

private:
  std::string path_;
};

std::error_code write_all(int fd, std::string_view contents) {
  std::size_t offset = 0;
  while (offset < contents.size()) {
    const ssize_t count =
        ::write(fd, contents.data() + offset, contents.size() - offset);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return errno_code();
    }
    if (count == 0) {
      return std::make_error_code(std::errc::io_error);
    }
    offset += static_cast<std::size_t>(count);
  }
  return {};
}

} // namespace

std::error_code publish_atomically(const std::filesystem::path &destination,
                                   std::string_view contents) {
  const std::filesystem::path parent =
      destination.has_parent_path() ? destination.parent_path() : ".";

  UniqueFd directory{::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  if (!directory.valid()) {
    return errno_code();
  }

  std::string pattern = destination.string() + ".tmp.XXXXXX";
  std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
  mutable_pattern.push_back('\0');

  UniqueFd temporary_fd{::mkstemp(mutable_pattern.data())};
  if (!temporary_fd.valid()) {
    return errno_code();
  }
  TemporaryPath temporary_path{mutable_pattern.data()};

  if (auto error = write_all(temporary_fd.get(), contents)) {
    return error;
  }
  if (::fsync(temporary_fd.get()) < 0) {
    return errno_code();
  }

  // A failed close can report a delayed write error, so do not silently bury it
  // in the RAII destructor on this success path.
  const int raw_fd = temporary_fd.release();
  if (::close(raw_fd) < 0) {
    return errno_code();
  }

  if (::rename(temporary_path.c_str(), destination.c_str()) < 0) {
    return errno_code();
  }
  temporary_path.release();

  // If this fails, the destination may already be visible, but its survival
  // across sudden power loss is unknown.  The error must reach the caller.
  if (::fsync(directory.get()) < 0) {
    return errno_code();
  }
  return {};
}

} // namespace aos_diy::snippets
