/*
 * aos_inst_write 的成功路徑，包括完整的寫入／讀取往返，以及傳輸格式的
 * 精確位元組檢查。
 */
#include "test_common.h"

#include <stdlib.h>
#include <string.h>

static size_t test_round_trip_one_record(void)
{
    const char *argv[] = { "prog", "arg1", "arg2" };
    aos_inst_t out;
    aos_inst_t in;
    aos_inst_state state;
    FILE *stream = tmpfile();

    CHECK(stream != NULL);
    fill_inst(&out, argv, 3U);
    out.stdin_path = "/dev/null";
    out.stdout_path = "/tmp/out";
    out.stderr_path = "/tmp/err";
    out.exit_path = "0";
    out.cwd = "/home/user";
    out.env_path = "/etc/env";
    out.extra = "note";

    state = aos_inst_write(stream, &out);
    CHECK(state == AOS_INST_OK);

    rewind(stream);
    aos_inst_init(&in);
    state = aos_inst_read(stream, &in);

    CHECK(state == AOS_INST_OK);
    CHECK(in.argc == 3U);
    CHECK(strcmp(in.argv[0], "prog") == 0);
    CHECK(strcmp(in.argv[1], "arg1") == 0);
    CHECK(strcmp(in.argv[2], "arg2") == 0);
    CHECK(in.argv[3] == NULL);
    CHECK(strcmp(in.stdin_path, "/dev/null") == 0);
    CHECK(strcmp(in.stdout_path, "/tmp/out") == 0);
    CHECK(strcmp(in.stderr_path, "/tmp/err") == 0);
    CHECK(strcmp(in.exit_path, "0") == 0);
    CHECK(strcmp(in.cwd, "/home/user") == 0);
    CHECK(strcmp(in.env_path, "/etc/env") == 0);
    CHECK(strcmp(in.extra, "note") == 0);

    aos_inst_free(&in);
    fclose(stream);
    return 1U;
}

static size_t test_round_trip_two_records(void)
{
    const char *argv1[] = { "one", "two" };
    const char *argv2[] = { "three" };
    aos_inst_t out1;
    aos_inst_t out2;
    aos_inst_t in;
    aos_inst_state state;
    FILE *stream = tmpfile();

    CHECK(stream != NULL);
    fill_inst(&out1, argv1, 2U);
    fill_inst(&out2, argv2, 1U);
    out2.cwd = "/somewhere";

    CHECK(aos_inst_write(stream, &out1) == AOS_INST_OK);
    CHECK(aos_inst_write(stream, &out2) == AOS_INST_OK);

    rewind(stream);
    aos_inst_init(&in);

    state = aos_inst_read(stream, &in);
    CHECK(state == AOS_INST_OK);
    CHECK(in.argc == 2U);
    CHECK(strcmp(in.argv[0], "one") == 0);
    CHECK(strcmp(in.argv[1], "two") == 0);

    state = aos_inst_read(stream, &in);
    CHECK(state == AOS_INST_OK);
    CHECK(in.argc == 1U);
    CHECK(strcmp(in.argv[0], "three") == 0);
    CHECK(strcmp(in.cwd, "/somewhere") == 0);

    state = aos_inst_read(stream, &in);
    CHECK(state == AOS_INST_EOF);

    aos_inst_free(&in);
    fclose(stream);
    return 1U;
}

/*
 * 鎖定傳輸格式本身：以定位字元分隔的 argv、LF 行尾，而且不只前七行，
 * 第八行也必須包含 LF。
 */
static size_t test_output_bytes_exact(void)
{
    const char *argv[] = { "a", "b" };
    aos_inst_t out;
    aos_inst_state state;
    FILE *stream = tmpfile();
    const char *expected =
        "a\tb\n"
        "in\n"
        "out\n"
        "err\n"
        "0\n"
        "/cwd\n"
        "env\n"
        "extra\n";
    long size;
    char *actual;
    size_t read_bytes;

    CHECK(stream != NULL);
    fill_inst(&out, argv, 2U);
    out.stdin_path = "in";
    out.stdout_path = "out";
    out.stderr_path = "err";
    out.exit_path = "0";
    out.cwd = "/cwd";
    out.env_path = "env";
    out.extra = "extra";

    state = aos_inst_write(stream, &out);
    CHECK(state == AOS_INST_OK);

    size = ftell(stream);
    CHECK(size == (long)strlen(expected));

    rewind(stream);
    actual = (char *)malloc((size_t)size + 1U);
    CHECK(actual != NULL);
    read_bytes = fread(actual, 1U, (size_t)size, stream);
    CHECK(read_bytes == (size_t)size);
    actual[size] = '\0';

    CHECK(strcmp(actual, expected) == 0);

    free(actual);
    fclose(stream);
    return 1U;
}

static size_t test_non_empty_fields_survive_round_trip(void)
{
    const char *argv[] = { "run" };
    aos_inst_t out;
    aos_inst_t in;
    aos_inst_state state;
    FILE *stream = tmpfile();

    CHECK(stream != NULL);
    fill_inst(&out, argv, 1U);
    out.stdin_path = "field-in";
    out.stdout_path = "field-out";
    out.stderr_path = "field-err";
    out.exit_path = "field-exit";
    out.cwd = "field-cwd";
    out.env_path = "field-env";
    out.extra = "field-extra";

    CHECK(aos_inst_write(stream, &out) == AOS_INST_OK);

    rewind(stream);
    aos_inst_init(&in);
    state = aos_inst_read(stream, &in);

    CHECK(state == AOS_INST_OK);
    CHECK(strcmp(in.stdin_path, "field-in") == 0);
    CHECK(strcmp(in.stdout_path, "field-out") == 0);
    CHECK(strcmp(in.stderr_path, "field-err") == 0);
    CHECK(strcmp(in.exit_path, "field-exit") == 0);
    CHECK(strcmp(in.cwd, "field-cwd") == 0);
    CHECK(strcmp(in.env_path, "field-env") == 0);
    CHECK(strcmp(in.extra, "field-extra") == 0);

    aos_inst_free(&in);
    fclose(stream);
    return 1U;
}

static size_t test_spaces_and_quotes_round_trip(void)
{
    const char *value = "he said \"hi\" 'there' \\ ok";
    const char *argv[] = { value };
    aos_inst_t out;
    aos_inst_t in;
    aos_inst_state state;
    FILE *stream = tmpfile();

    CHECK(stream != NULL);
    fill_inst(&out, argv, 1U);

    CHECK(aos_inst_write(stream, &out) == AOS_INST_OK);

    rewind(stream);
    aos_inst_init(&in);
    state = aos_inst_read(stream, &in);

    CHECK(state == AOS_INST_OK);
    CHECK(in.argc == 1U);
    CHECK(strcmp(in.argv[0], value) == 0);

    aos_inst_free(&in);
    fclose(stream);
    return 1U;
}

size_t run_inst_write_tests(void)
{
    size_t count = 0U;

    count += test_round_trip_one_record();
    count += test_round_trip_two_records();
    count += test_output_bytes_exact();
    count += test_non_empty_fields_survive_round_trip();
    count += test_spaces_and_quotes_round_trip();

    return count;
}
