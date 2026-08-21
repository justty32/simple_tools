#pragma once

#include <aos/export.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace aos {

constexpr std::size_t kMaxArgs = 256;
constexpr std::size_t kMaxEnv = 256;
constexpr std::size_t kMaxJsonDepth = 3;

enum class InstState {
    Ok,
    InvalidArgument,
    JsonSyntax,
    NotAnObject,
    UnknownKey,
    FieldTypeMismatch,
    EmptyArgv,
    TooManyArgs,
    TooManyEnv,
    EnvKeyInvalid,
    DepthExceeded,
    RecordTooLong,
    TotalTooLong,
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
AOS_API std::size_t max_args() noexcept;
AOS_API std::size_t max_env() noexcept;
AOS_API std::size_t max_json_depth() noexcept;

}  // namespace aos
