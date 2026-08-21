#define _POSIX_C_SOURCE 200809L

#include <aos/aos.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    static const char record[] =
        "{\"argv\":[\"/bin/sh\",\"-c\",\"exit 7\"],"
        "\"cwd\":\"/\",\"env\":{\"AOS_C_TEST\":\"yes\"},"
        "\"timeout_ms\":1000}";
    aos_instruction *instruction = aos_instruction_new();
    aos_exec_result result;
    size_t size = 0;
    char *output;
    int fds[2];

    assert(instruction != NULL);
    assert(strcmp(aos_version_string(), "0.1.0") == 0);
    assert(aos_inst_argv_max() == 256);
    assert(aos_inst_env_max() == 256);
    assert(aos_inst_json_depth_max() == 3);

    assert(aos_instruction_read_buffer(record, sizeof(record) - 1,
                                       instruction) == AOS_INST_OK);
    assert(aos_instruction_argc(instruction) == 3);
    assert(strcmp(aos_instruction_arg(instruction, 0), "/bin/sh") == 0);
    assert(strcmp(aos_instruction_field(instruction, AOS_FIELD_CWD), "/") == 0);
    assert(aos_instruction_env_count(instruction) == 1);
    assert(strcmp(aos_instruction_env_key(instruction, 0), "AOS_C_TEST") == 0);
    assert(strcmp(aos_instruction_env_value(instruction, 0), "yes") == 0);
    assert(aos_instruction_timeout_ms(instruction) == 1000);

    assert(aos_instruction_write_buffer(instruction, NULL, 0, &size) ==
           AOS_INST_BUFFER_TOO_SMALL);
    assert(size != 0);
    output = (char *)malloc(size + 1);
    assert(output != NULL);
    assert(aos_instruction_write_buffer(instruction, output, size + 1, &size) ==
           AOS_INST_OK);
    assert(output[size - 1] == '\n');
    assert(output[size] == '\0');
    free(output);

    assert(aos_instruction_execute(instruction, &result) == AOS_EXEC_OK);
    assert(result.status == 7);
    assert(result.signalled == 0);
    assert(result.timed_out == 0);

    aos_instruction_clear(instruction);
    assert(aos_instruction_push_arg(instruction, "echo") == AOS_INST_OK);
    assert(aos_instruction_set_field(instruction, AOS_FIELD_STDIN, "in") == AOS_INST_OK);
    assert(aos_instruction_set_field(instruction, AOS_FIELD_STDOUT, "out") == AOS_INST_OK);
    assert(aos_instruction_set_field(instruction, AOS_FIELD_STDERR, "err") == AOS_INST_OK);
    assert(aos_instruction_set_field(instruction, AOS_FIELD_EXIT, "status") == AOS_INST_OK);
    assert(aos_instruction_set_field(instruction, AOS_FIELD_CWD, "/tmp") == AOS_INST_OK);
    assert(aos_instruction_set_env(instruction, "KEY", "value") == AOS_INST_OK);
    assert(aos_instruction_set_timeout_ms(instruction, 42) == AOS_INST_OK);

    assert(pipe(fds) == 0);
    assert(write(fds[1], record, sizeof(record) - 1) == (ssize_t)(sizeof(record) - 1));
    assert(close(fds[1]) == 0);
    assert(aos_instruction_read_fd(fds[0], instruction) == AOS_INST_OK);
    assert(close(fds[0]) == 0);

    assert(strcmp(aos_inst_state_string(AOS_INST_JSON_SYNTAX), "JsonSyntax") == 0);
    assert(strcmp(aos_exec_state_string(AOS_EXEC_OK), "ok") == 0);
    aos_instruction_free(instruction);
    return 0;
}
