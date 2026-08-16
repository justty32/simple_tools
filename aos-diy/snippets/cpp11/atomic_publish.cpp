#include "atomic_publish.hpp"

#include "unique_fd.hpp"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace aos_diy {
namespace snippets11 {
namespace {

std::error_code errno_code() {
  return std::error_code(errno, std::generic_category());
}

std::string parent_directory(const std::string &path) {
  const std::string::size_type slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  if (slash == 0U) {
    return "/";
  }
  return path.substr(0U, slash);
}

class TemporaryPath {
public:
  explicit TemporaryPath(const std::string &path) : path_(path) {}
  ~TemporaryPath() {
    if (!path_.empty()) {
      (void)::unlink(path_.c_str());
    }
  }
  const char *c_str() const { return path_.c_str(); }
  void release() { path_.clear(); }

private:
  TemporaryPath(const TemporaryPath &);
  TemporaryPath &operator=(const TemporaryPath &);
  std::string path_;
};

std::error_code write_all(int fd, const std::string &contents) {
  std::size_t offset = 0U;
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
  return std::error_code();
}

} // namespace

std::error_code publish_atomically(const std::string &destination,
                                   const std::string &contents) {
  const std::string directory_name = parent_directory(destination);
  UniqueFd directory{
      ::open(directory_name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  if (!directory.valid()) {
    return errno_code();
  }

  const std::string pattern = destination + ".tmp.XXXXXX";
  std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
  mutable_pattern.push_back('\0');
  UniqueFd temporary_fd{::mkstemp(&mutable_pattern[0])};
  if (!temporary_fd.valid()) {
    return errno_code();
  }
  TemporaryPath temporary_path(&mutable_pattern[0]);

  std::error_code error = write_all(temporary_fd.get(), contents);
  if (error) {
    return error;
  }
  if (::fsync(temporary_fd.get()) < 0) {
    return errno_code();
  }
  const int raw_fd = temporary_fd.release();
  if (::close(raw_fd) < 0) {
    return errno_code();
  }
  if (::rename(temporary_path.c_str(), destination.c_str()) < 0) {
    return errno_code();
  }
  temporary_path.release();
  if (::fsync(directory.get()) < 0) {
    return errno_code();
  }
  return std::error_code();
}

} // namespace snippets11
} // namespace aos_diy
