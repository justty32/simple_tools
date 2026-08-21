#define _POSIX_C_SOURCE 200809L

#include <aos/aos.h>

#include "capi_common.hpp"

#include <aos/exec.hpp>
#include <aos/format.hpp>
#include <aos/inst.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <unistd.h>

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

}  // namespace

extern "C" {

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

}  // extern "C"
