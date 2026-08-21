#define _POSIX_C_SOURCE 200809L

#include <aos/aos.h>

#include <aos/exec.hpp>
#include <aos/format.hpp>
#include <aos/inst.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <iterator>
#include <string>

#include <fcntl.h>
#include <unistd.h>

struct aos_instruction {
    aos::inst_t value;
};

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

namespace {

aos_inst_state inst_state(aos::InstState state) {
    return static_cast<aos_inst_state>(state);
}

bool set_cloexec(int fd) {
    int flags;
    do { flags = fcntl(fd, F_GETFD); } while (flags < 0 && errno == EINTR);
    if (flags < 0) return false;
    int result;
    do { result = fcntl(fd, F_SETFD, flags | FD_CLOEXEC); }
    while (result < 0 && errno == EINTR);
    return result == 0;
}

const std::pair<const std::string *, const std::string *> *env_at(
    const aos_instruction *instruction, size_t index,
    std::pair<const std::string *, const std::string *> &entry) {
    if (instruction == nullptr || index >= instruction->value.env.size()) {
        return nullptr;
    }
    auto it = instruction->value.env.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(index));
    entry = {&it->first, &it->second};
    return &entry;
}

}  // namespace

extern "C" {

AOS_API aos_instruction *aos_instruction_new(void) {
    try { return new aos_instruction; } catch (...) { return nullptr; }
}

AOS_API void aos_instruction_free(aos_instruction *instruction) {
    try { delete instruction; } catch (...) {}
}

AOS_API void aos_instruction_clear(aos_instruction *instruction) {
    try {
        if (instruction != nullptr) instruction->value.clear();
    } catch (...) {}
}

AOS_API size_t aos_instruction_argc(const aos_instruction *instruction) {
    try { return instruction == nullptr ? 0 : instruction->value.argv.size(); }
    catch (...) { return 0; }
}

AOS_API const char *aos_instruction_arg(
    const aos_instruction *instruction, size_t index) {
    try {
        if (instruction == nullptr || index >= instruction->value.argv.size()) return nullptr;
        return instruction->value.argv[index].c_str();
    } catch (...) { return nullptr; }
}

AOS_API aos_inst_state aos_instruction_push_arg(
    aos_instruction *instruction, const char *value) {
    try {
        if (instruction == nullptr || value == nullptr) return AOS_INST_INVALID_ARGUMENT;
        if (instruction->value.argv.size() >= aos::kMaxArgs) return AOS_INST_TOO_MANY_ARGS;
        instruction->value.argv.emplace_back(value);
        return AOS_INST_OK;
    } catch (...) { return AOS_INST_ALLOC_FAILED; }
}

AOS_API const char *aos_instruction_field(
    const aos_instruction *instruction, aos_inst_field field) {
    try {
        if (instruction == nullptr) return nullptr;
        switch (field) {
        case AOS_FIELD_STDIN: return instruction->value.stdin_path.c_str();
        case AOS_FIELD_STDOUT: return instruction->value.stdout_path.c_str();
        case AOS_FIELD_STDERR: return instruction->value.stderr_path.c_str();
        case AOS_FIELD_EXIT: return instruction->value.exit_path.c_str();
        case AOS_FIELD_CWD: return instruction->value.cwd.c_str();
        }
        return nullptr;
    } catch (...) { return nullptr; }
}

AOS_API aos_inst_state aos_instruction_set_field(
    aos_instruction *instruction, aos_inst_field field, const char *value) {
    try {
        if (instruction == nullptr || value == nullptr) return AOS_INST_INVALID_ARGUMENT;
        std::string *destination = nullptr;
        switch (field) {
        case AOS_FIELD_STDIN: destination = &instruction->value.stdin_path; break;
        case AOS_FIELD_STDOUT: destination = &instruction->value.stdout_path; break;
        case AOS_FIELD_STDERR: destination = &instruction->value.stderr_path; break;
        case AOS_FIELD_EXIT: destination = &instruction->value.exit_path; break;
        case AOS_FIELD_CWD: destination = &instruction->value.cwd; break;
        }
        if (destination == nullptr) return AOS_INST_INVALID_ARGUMENT;
        *destination = value;
        return AOS_INST_OK;
    } catch (...) { return AOS_INST_ALLOC_FAILED; }
}

AOS_API size_t aos_instruction_env_count(const aos_instruction *instruction) {
    try { return instruction == nullptr ? 0 : instruction->value.env.size(); }
    catch (...) { return 0; }
}

AOS_API const char *aos_instruction_env_key(
    const aos_instruction *instruction, size_t index) {
    try {
        std::pair<const std::string *, const std::string *> entry;
        return env_at(instruction, index, entry) == nullptr ? nullptr : entry.first->c_str();
    } catch (...) { return nullptr; }
}

AOS_API const char *aos_instruction_env_value(
    const aos_instruction *instruction, size_t index) {
    try {
        std::pair<const std::string *, const std::string *> entry;
        return env_at(instruction, index, entry) == nullptr ? nullptr : entry.second->c_str();
    } catch (...) { return nullptr; }
}

AOS_API aos_inst_state aos_instruction_set_env(
    aos_instruction *instruction, const char *key, const char *value) {
    try {
        if (instruction == nullptr || key == nullptr || value == nullptr)
            return AOS_INST_INVALID_ARGUMENT;
        if (key[0] == '\0' || std::strchr(key, '=') != nullptr)
            return AOS_INST_ENV_KEY_INVALID;
        auto it = instruction->value.env.find(key);
        if (it == instruction->value.env.end() &&
            instruction->value.env.size() >= aos::kMaxEnv)
            return AOS_INST_TOO_MANY_ENV;
        instruction->value.env[key] = value;
        return AOS_INST_OK;
    } catch (...) { return AOS_INST_ALLOC_FAILED; }
}

AOS_API uint64_t aos_instruction_timeout_ms(
    const aos_instruction *instruction) {
    try { return instruction == nullptr ? 0 : instruction->value.timeout_ms; }
    catch (...) { return 0; }
}

AOS_API aos_inst_state aos_instruction_set_timeout_ms(
    aos_instruction *instruction, uint64_t value) {
    try {
        if (instruction == nullptr) return AOS_INST_INVALID_ARGUMENT;
        instruction->value.timeout_ms = value;
        return AOS_INST_OK;
    } catch (...) { return AOS_INST_ALLOC_FAILED; }
}

AOS_API aos_inst_state aos_instruction_read_buffer(
    const char *data, size_t size, aos_instruction *instruction) {
    try {
        if (instruction == nullptr) return AOS_INST_INVALID_ARGUMENT;
        return inst_state(aos::read_one(data, size, instruction->value));
    } catch (...) { return AOS_INST_ALLOC_FAILED; }
}

AOS_API aos_inst_state aos_instruction_read_fd(int fd, aos_instruction *instruction) {
    try {
        if (instruction == nullptr || fd < 0) return AOS_INST_INVALID_ARGUMENT;
        instruction->value.clear();
        if (!set_cloexec(fd)) return AOS_INST_READ_ERROR;
        aos::ReadOptions options;
        std::string input;
        std::array<char, 8192> chunk{};
        for (;;) {
            ssize_t count;
            do { count = read(fd, chunk.data(), chunk.size()); }
            while (count < 0 && errno == EINTR);
            if (count < 0) return AOS_INST_READ_ERROR;
            if (count == 0) break;
            if (input.size() > options.max_total_bytes - static_cast<size_t>(count))
                return AOS_INST_TOTAL_TOO_LONG;
            input.append(chunk.data(), static_cast<size_t>(count));
        }
        return inst_state(aos::read_one(input.data(), input.size(),
                                        instruction->value, options));
    } catch (...) { return AOS_INST_ALLOC_FAILED; }
}

AOS_API aos_inst_state aos_instruction_write_buffer(
    const aos_instruction *instruction, char *buffer, size_t size,
    size_t *needed) {
    try {
        if (needed != nullptr) *needed = 0;
        if (instruction == nullptr || needed == nullptr) return AOS_INST_INVALID_ARGUMENT;
        std::string output;
        const aos::InstState state = aos::write_one(instruction->value, output);
        if (state != aos::InstState::Ok) return inst_state(state);
        const size_t required = output.size();
        *needed = required;
        if (buffer == nullptr || size <= required) {
            return AOS_INST_BUFFER_TOO_SMALL;
        }
        std::memcpy(buffer, output.data(), required);
        buffer[required] = '\0';
        return AOS_INST_OK;
    } catch (...) { return AOS_INST_ALLOC_FAILED; }
}

AOS_API aos_exec_state aos_instruction_execute(
    aos_instruction *instruction, aos_exec_result *result) {
    try {
        if (result != nullptr) *result = aos_exec_result{};
        if (instruction == nullptr) return AOS_EXEC_INVALID_ARGUMENT;
        aos::ExecResult cpp_result;
        const aos::ExecState state = aos::execute(instruction->value, cpp_result);
        if (result != nullptr) {
            result->status = cpp_result.status;
            result->signalled = cpp_result.signalled ? 1 : 0;
            result->timed_out = cpp_result.timed_out ? 1 : 0;
            result->error = cpp_result.error;
        }
        return static_cast<aos_exec_state>(state);
    } catch (...) { return AOS_EXEC_ALLOC_FAILED; }
}

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
