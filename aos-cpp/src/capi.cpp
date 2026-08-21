#include <aos/aos.h>

#include <aos/exec.hpp>
#include <aos/format.hpp>
#include <aos/inst.hpp>

static_assert(static_cast<int>(aos::InstState::Ok) == AOS_INST_OK);
static_assert(static_cast<int>(aos::InstState::InvalidArgument) ==
              AOS_INST_INVALID_ARGUMENT);
static_assert(static_cast<int>(aos::InstState::JsonSyntax) == AOS_INST_JSON_SYNTAX);
static_assert(static_cast<int>(aos::InstState::NotAnObject) == AOS_INST_NOT_AN_OBJECT);
static_assert(static_cast<int>(aos::InstState::UnknownKey) == AOS_INST_UNKNOWN_KEY);
static_assert(static_cast<int>(aos::InstState::FieldTypeMismatch) ==
              AOS_INST_FIELD_TYPE_MISMATCH);
static_assert(static_cast<int>(aos::InstState::EmptyArgv) == AOS_INST_EMPTY_ARGV);
static_assert(static_cast<int>(aos::InstState::TooManyArgs) == AOS_INST_TOO_MANY_ARGS);
static_assert(static_cast<int>(aos::InstState::TooManyEnv) == AOS_INST_TOO_MANY_ENV);
static_assert(static_cast<int>(aos::InstState::EnvKeyInvalid) ==
              AOS_INST_ENV_KEY_INVALID);
static_assert(static_cast<int>(aos::InstState::DepthExceeded) ==
              AOS_INST_DEPTH_EXCEEDED);
static_assert(static_cast<int>(aos::InstState::RecordTooLong) ==
              AOS_INST_RECORD_TOO_LONG);
static_assert(static_cast<int>(aos::InstState::TotalTooLong) ==
              AOS_INST_TOTAL_TOO_LONG);

static_assert(static_cast<int>(aos::ExecState::Ok) == AOS_EXEC_OK);
static_assert(static_cast<int>(aos::ExecState::InvalidArgument) ==
              AOS_EXEC_INVALID_ARGUMENT);
static_assert(static_cast<int>(aos::ExecState::SpawnFailed) ==
              AOS_EXEC_SPAWN_FAILED);
static_assert(static_cast<int>(aos::ExecState::WaitFailed) == AOS_EXEC_WAIT_FAILED);
static_assert(static_cast<int>(aos::ExecState::ExitWriteFailed) ==
              AOS_EXEC_EXIT_WRITE_FAILED);

extern "C" {

AOS_API size_t aos_inst_argv_max(void) {
    try { return aos::max_args(); } catch (...) { return 0; }
}
AOS_API size_t aos_inst_env_max(void) {
    try { return aos::max_env(); } catch (...) { return 0; }
}
AOS_API size_t aos_inst_json_depth_max(void) {
    try { return aos::max_json_depth(); } catch (...) { return 0; }
}
AOS_API size_t aos_inst_record_max_bytes(void) {
    try { return aos::ReadOptions{}.max_record_bytes; } catch (...) { return 0; }
}
AOS_API size_t aos_inst_total_max_bytes(void) {
    try { return aos::ReadOptions{}.max_total_bytes; } catch (...) { return 0; }
}

AOS_API const char *aos_inst_state_string(aos_inst_state state) {
    try {
        if (state >= AOS_INST_OK && state <= AOS_INST_TOTAL_TOO_LONG)
            return aos::to_string(static_cast<aos::InstState>(state));
        switch (state) {
        case AOS_INST_ALLOC_FAILED: return "AllocationFailed";
        case AOS_INST_READ_ERROR: return "ReadError";
        case AOS_INST_BUFFER_TOO_SMALL: return "BufferTooSmall";
        default: return "Unknown";
        }
    } catch (...) { return nullptr; }
}

AOS_API const char *aos_exec_state_string(aos_exec_state state) {
    try {
        if (state >= AOS_EXEC_OK && state <= AOS_EXEC_EXIT_WRITE_FAILED)
            return aos::to_string(static_cast<aos::ExecState>(state));
        return state == AOS_EXEC_ALLOC_FAILED ? "allocation failed" :
                                                "unknown execution result";
    } catch (...) { return nullptr; }
}

AOS_API const char *aos_version_string(void) {
    try { return "0.1.0"; } catch (...) { return nullptr; }
}

}  // extern "C"
