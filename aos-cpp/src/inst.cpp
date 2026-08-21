#include <aos/inst.hpp>

namespace aos {

void inst_t::clear() noexcept {
    argv.clear();
    stdin_path.clear();
    stdout_path.clear();
    stderr_path.clear();
    exit_path.clear();
    cwd.clear();
    env.clear();
    timeout_ms = 0;
}

const char *to_string(InstState state) noexcept {
    switch (state) {
    case InstState::Ok: return "Ok";
    case InstState::InvalidArgument: return "InvalidArgument";
    case InstState::JsonSyntax: return "JsonSyntax";
    case InstState::NotAnObject: return "NotAnObject";
    case InstState::UnknownKey: return "UnknownKey";
    case InstState::FieldTypeMismatch: return "FieldTypeMismatch";
    case InstState::EmptyArgv: return "EmptyArgv";
    case InstState::TooManyArgs: return "TooManyArgs";
    case InstState::TooManyEnv: return "TooManyEnv";
    case InstState::EnvKeyInvalid: return "EnvKeyInvalid";
    case InstState::DepthExceeded: return "DepthExceeded";
    case InstState::RecordTooLong: return "RecordTooLong";
    case InstState::TotalTooLong: return "TotalTooLong";
    }
    return "Unknown";
}

std::size_t max_args() noexcept { return kMaxArgs; }
std::size_t max_env() noexcept { return kMaxEnv; }
std::size_t max_json_depth() noexcept { return kMaxJsonDepth; }

}  // namespace aos
