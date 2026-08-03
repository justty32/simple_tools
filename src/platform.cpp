#include "platform.hpp"
#include <cstdlib>
#include <iostream>

namespace dcap {

std::string exe_suffix() {
#ifdef _WIN32
    return ".exe";
#else
    return "";
#endif
}

static const char* env_or_null(const char* key) {
    const char* v = std::getenv(key);
    return (v && *v) ? v : nullptr;
}

std::string dcap_home() {
    if (const char* v = env_or_null("DCAP_HOME"))
        return v;
#ifdef _WIN32
    return "C:/dev/dcap";
#else
    const char* home = env_or_null("HOME");
    return std::string(home ? home : ".") + "/dev/dcap";
#endif
}

std::string make_program() {
#ifdef _WIN32
    if (std::system("mingw32-make --version >NUL 2>&1") == 0)
        return "mingw32-make";
    return "make";
#else
    if (std::system("make --version >/dev/null 2>&1") == 0)
        return "make";
    return "mingw32-make";
#endif
}

int run(const std::string& cmd) {
    // Flush our own buffered output first so child process output (which goes
    // straight to the console) does not interleave ahead of our messages.
    std::cout.flush();
    std::cerr.flush();
    return std::system(cmd.c_str());
}

} // namespace dcap
