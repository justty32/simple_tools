#ifndef AOS_AOS_H
#define AOS_AOS_H

#include <aos/export.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AOS_VERSION_MAJOR 0
#define AOS_VERSION_MINOR 1
#define AOS_VERSION_PATCH 0

/* ABI rule: existing enum values are frozen. New values may only be appended. */
typedef enum aos_inst_state {
    AOS_INST_OK = 0,
    AOS_INST_INVALID_ARGUMENT = 1,
    AOS_INST_JSON_SYNTAX = 2,
    AOS_INST_NOT_AN_OBJECT = 3,
    AOS_INST_UNKNOWN_KEY = 4,
    AOS_INST_FIELD_TYPE_MISMATCH = 5,
    AOS_INST_EMPTY_ARGV = 6,
    AOS_INST_TOO_MANY_ARGS = 7,
    AOS_INST_TOO_MANY_ENV = 8,
    AOS_INST_ENV_KEY_INVALID = 9,
    AOS_INST_DEPTH_EXCEEDED = 10,
    AOS_INST_RECORD_TOO_LONG = 11,
    AOS_INST_TOTAL_TOO_LONG = 12,
    AOS_INST_ALLOC_FAILED = 13,
    AOS_INST_READ_ERROR = 14,
    AOS_INST_BUFFER_TOO_SMALL = 15
} aos_inst_state;

/* ABI rule: existing enum values are frozen. New values may only be appended. */
typedef enum aos_inst_field {
    AOS_FIELD_STDIN = 0,
    AOS_FIELD_STDOUT = 1,
    AOS_FIELD_STDERR = 2,
    AOS_FIELD_EXIT = 3,
    AOS_FIELD_CWD = 4
} aos_inst_field;

/* ABI rule: existing enum values are frozen. New values may only be appended. */
typedef enum aos_exec_state {
    AOS_EXEC_OK = 0,
    AOS_EXEC_INVALID_ARGUMENT = 1,
    AOS_EXEC_SPAWN_FAILED = 2,
    AOS_EXEC_WAIT_FAILED = 3,
    AOS_EXEC_EXIT_WRITE_FAILED = 4,
    AOS_EXEC_ALLOC_FAILED = 5
} aos_exec_state;

typedef struct aos_instruction aos_instruction;

typedef struct aos_exec_result {
    int status;
    int signalled;
    int timed_out;
    int error;
} aos_exec_result;

AOS_API aos_instruction *aos_instruction_new(void);
AOS_API void aos_instruction_free(aos_instruction *instruction);
AOS_API void aos_instruction_clear(aos_instruction *instruction);

AOS_API size_t aos_instruction_argc(const aos_instruction *instruction);
AOS_API const char *aos_instruction_arg(
    const aos_instruction *instruction, size_t index);
AOS_API aos_inst_state aos_instruction_push_arg(
    aos_instruction *instruction, const char *value);

AOS_API const char *aos_instruction_field(
    const aos_instruction *instruction, aos_inst_field field);
AOS_API aos_inst_state aos_instruction_set_field(
    aos_instruction *instruction, aos_inst_field field, const char *value);

AOS_API size_t aos_instruction_env_count(const aos_instruction *instruction);
AOS_API const char *aos_instruction_env_key(
    const aos_instruction *instruction, size_t index);
AOS_API const char *aos_instruction_env_value(
    const aos_instruction *instruction, size_t index);
AOS_API aos_inst_state aos_instruction_set_env(
    aos_instruction *instruction, const char *key, const char *value);

AOS_API uint64_t aos_instruction_timeout_ms(
    const aos_instruction *instruction);
AOS_API aos_inst_state aos_instruction_set_timeout_ms(
    aos_instruction *instruction, uint64_t value);

AOS_API aos_inst_state aos_instruction_read_buffer(
    const char *data, size_t size, aos_instruction *instruction);
AOS_API aos_inst_state aos_instruction_read_fd(
    int fd, aos_instruction *instruction);
AOS_API aos_inst_state aos_instruction_write_buffer(
    const aos_instruction *instruction, char *buffer, size_t size,
    size_t *needed);

AOS_API aos_exec_state aos_instruction_execute(
    aos_instruction *instruction, aos_exec_result *result);

AOS_API size_t aos_inst_argv_max(void);
AOS_API size_t aos_inst_env_max(void);
AOS_API size_t aos_inst_json_depth_max(void);
AOS_API size_t aos_inst_record_max_bytes(void);
AOS_API size_t aos_inst_total_max_bytes(void);
AOS_API const char *aos_inst_state_string(aos_inst_state state);
AOS_API const char *aos_exec_state_string(aos_exec_state state);
AOS_API const char *aos_version_string(void);

#ifdef __cplusplus
}
#endif

#endif
