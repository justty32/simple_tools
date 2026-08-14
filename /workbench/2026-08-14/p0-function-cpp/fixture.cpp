#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
void write_all(int fd, const char* p, std::size_t n) {
  while (n != 0) {
    const auto written = ::write(fd, p, n);
    if (written > 0) { p += written; n -= static_cast<std::size_t>(written); continue; }
    if (written < 0 && errno == EINTR) continue;
    return;
  }
}
void json_string(const std::string& s) {
  std::cout << '"';
  for (unsigned char c : s) {
    switch (c) { case '\\': std::cout << "\\\\"; break; case '"': std::cout << "\\\""; break;
      case '\n': std::cout << "\\n"; break; case '\r': std::cout << "\\r"; break; case '\t': std::cout << "\\t"; break;
      default: if (c < 0x20) { static constexpr char hex[] = "0123456789abcdef"; std::cout << "\\u00" << hex[c >> 4] << hex[c & 15]; } else std::cout << static_cast<char>(c); }
  }
  std::cout << '"';
}
}

int main(int argc, char** argv) {
  if (argc < 2) return 64;
  const std::string command(argv[1]);
  if (command == "argv") {
    std::cout << '[';
    for (int i = 2; i < argc; ++i) { if (i != 2) std::cout << ','; json_string(argv[i]); }
    std::cout << ']';
  } else if (command == "echo" || command == "delay-read") {
    if (command == "delay-read" && argc > 2) ::usleep(static_cast<useconds_t>(std::strtoul(argv[2], nullptr, 10)) * 1000U);
    char buffer[65536];
    for (;;) {
      const auto n = ::read(STDIN_FILENO, buffer, sizeof buffer);
      if (n > 0) write_all(STDOUT_FILENO, buffer, static_cast<std::size_t>(n));
      else if (n < 0 && errno == EINTR) continue;
      else break;
    }
  } else if (command == "split") {
    if (argc < 3) return 64;
    const auto amount = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    std::string out(65536, 'O'), err(65536, 'E');
    for (std::size_t n = amount; n; ) { const auto chunk = std::min(n, out.size()); write_all(STDOUT_FILENO, out.data(), chunk); n -= chunk; }
    for (std::size_t n = amount; n; ) { const auto chunk = std::min(n, err.size()); write_all(STDERR_FILENO, err.data(), chunk); n -= chunk; }
    return 7;
  } else if (command == "sigterm") {
    ::kill(::getpid(), SIGTERM);
  } else if (command == "sigpipe-default") {
    struct sigaction current{};
    if (::sigaction(SIGPIPE, nullptr, &current) != 0) return 70;
    if (current.sa_handler != SIG_DFL) return 71;
    write_all(STDOUT_FILENO, "default\n", 8);
  } else if (command == "cwd") {
    char buffer[4096];
    if (::getcwd(buffer, sizeof buffer)) { write_all(STDOUT_FILENO, buffer, std::strlen(buffer)); write_all(STDOUT_FILENO, "\n", 1); }
  } else return 64;
  return 0;
}
