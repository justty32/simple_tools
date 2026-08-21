#include <aos/aos.h>

#include "capi_common.hpp"

#include <cstring>
#include <iterator>
#include <string>

namespace {

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

}  // extern "C"
