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
static_assert(static_cast<int>(aos::InstState::EnvKeyInvalid) ==
              AOS_INST_ENV_KEY_INVALID);

static_assert(static_cast<int>(aos::ExecState::Ok) == AOS_EXEC_OK);
static_assert(static_cast<int>(aos::ExecState::InvalidArgument) ==
              AOS_EXEC_INVALID_ARGUMENT);
static_assert(static_cast<int>(aos::ExecState::SpawnFailed) ==
              AOS_EXEC_SPAWN_FAILED);
static_assert(static_cast<int>(aos::ExecState::WaitFailed) == AOS_EXEC_WAIT_FAILED);
static_assert(static_cast<int>(aos::ExecState::ExitWriteFailed) ==
              AOS_EXEC_EXIT_WRITE_FAILED);

extern "C" {

AOS_API const char *aos_inst_state_string(aos_inst_state state) {
    try {
        switch (state) {
        case AOS_INST_OK:
        case AOS_INST_INVALID_ARGUMENT:
        case AOS_INST_JSON_SYNTAX:
        case AOS_INST_NOT_AN_OBJECT:
        case AOS_INST_UNKNOWN_KEY:
        case AOS_INST_FIELD_TYPE_MISMATCH:
        case AOS_INST_EMPTY_ARGV:
        case AOS_INST_ENV_KEY_INVALID:
            return aos::to_string(static_cast<aos::InstState>(state));
        case AOS_INST_ALLOC_FAILED: return "AllocationFailed";
        case AOS_INST_READ_ERROR: return "ReadError";
        case AOS_INST_WRITE_ERROR: return "WriteError";
        case AOS_INST_BUFFER_TOO_SMALL: return "BufferTooSmall";
        }
        return "Unknown";
    } catch (...) { return nullptr; }
}

AOS_API const char *aos_exec_state_string(aos_exec_state state) {
    try {
        switch (state) {
        case AOS_EXEC_OK:
        case AOS_EXEC_INVALID_ARGUMENT:
        case AOS_EXEC_SPAWN_FAILED:
        case AOS_EXEC_WAIT_FAILED:
        case AOS_EXEC_EXIT_WRITE_FAILED:
            return aos::to_string(static_cast<aos::ExecState>(state));
        case AOS_EXEC_ALLOC_FAILED: return "AllocationFailed";
        }
        return "Unknown";
    } catch (...) { return nullptr; }
}

AOS_API const char *aos_version_string(void) {
    try { return "0.1.0"; } catch (...) { return nullptr; }
}

}  // extern "C"
