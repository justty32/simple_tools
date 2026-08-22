#pragma once

#include <aos/export.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aos {

enum class InstState {
    Ok,
    InvalidArgument,
    JsonSyntax,
    NotAnObject,
    UnknownKey,
    FieldTypeMismatch,
    EmptyArgv,
    EnvKeyInvalid,
};

struct inst_t {
    std::vector<std::string> argv;
    std::string stdin_path;
    std::string stdout_path;
    std::string stderr_path;
    std::string exit_path;
    std::string cwd;
    std::map<std::string, std::string> env;
    std::uint64_t timeout_ms = 0;

    void clear() noexcept;
};

AOS_API const char *to_string(InstState state) noexcept;

}  // namespace aos
