/*
 * tests/test_capi.c
 *
 * 公開 C ABI（<aos/aos.h>）的驗證測試。刻意用 C99 編譯，且編譯指令只給
 * -Iinclude；這是「這道邊界真的只需要一個 C 編譯器」唯一站得住腳的證明，
 * 所以本檔案不得引入 tests/ 底下任何東西（test_check.hpp 是 C++），也不得
 * 引入 aos/inst.hpp 或 aos/exec.hpp。
 *
 * mkdtemp() 是 POSIX 介面。-std=c99 會定義 __STRICT_ANSI__，把它從
 * <stdlib.h> 藏起來，所以這個巨集必須搶在任何標頭之前定義。
 */
#define _POSIX_C_SOURCE 200809L

#include "aos/aos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 不能重用 tests/test_check.hpp，那是 C++ 標頭。這裡自備一份等價、純
 * C99 的版本：印出檔名、行號與運算式文字，然後結束程序。刻意不透過
 * assert()，因為 -DNDEBUG 會讓它消失而看起來一切正常。
 */
#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                    __LINE__, #expr); \
            exit(1); \
        } \
    } while (0)

/* 單一字串欄位的列舉值，依序列化順序排列，供多個測試共用。 */
#define FIELD_COUNT 6
static const aos_inst_field kAllFields[FIELD_COUNT] = {
    AOS_FIELD_STDIN, AOS_FIELD_STDOUT, AOS_FIELD_STDERR, AOS_FIELD_EXIT,
    AOS_FIELD_CWD,   AOS_FIELD_EXTRA
};

static FILE *must_tmpfile(void)
{
    FILE *f = tmpfile();

    CHECK(f != NULL);
    return f;
}

/*
 * 建一個帶有引數、env 與全部字串欄位的樣本指令，供 write_buffer 系列測試
 * 共用，避免每個測試都重新拼一次同樣的內容。
 */
static aos_instruction *make_sample_instruction(void)
{
    static const char *argv[3] = { "prog", "a b", "c" };
    aos_instruction *inst = aos_instruction_new();
    size_t i;

    CHECK(inst != NULL);
    for (i = 0; i < 3; ++i) {
        CHECK(aos_instruction_push_arg(inst, argv[i]) == AOS_INST_OK);
    }
    CHECK(aos_instruction_push_env(inst, "K0=v0") == AOS_INST_OK);
    CHECK(aos_instruction_push_env(inst, "K1=v 1") == AOS_INST_OK);
    for (i = 0; i < FIELD_COUNT; ++i) {
        char value[16];

        snprintf(value, sizeof(value), "buf-field-%zu", i);
        CHECK(aos_instruction_set_field(inst, kAllFields[i], value) ==
              AOS_INST_OK);
    }
    return inst;
}

static size_t test_lifecycle(void)
{
    aos_instruction *inst = aos_instruction_new();

    CHECK(inst != NULL);
    CHECK(aos_instruction_argc(inst) == 0);
    aos_instruction_free(inst);

    /* free(NULL) 必須是無事發生，而不是對空指標解參照。 */
    aos_instruction_free(NULL);
    return 1;
}

static size_t test_null_accessors(void)
{
    /* 三個讀取函式面對 NULL 控制代碼時，都要回傳「空」而不是當機。 */
    CHECK(aos_instruction_argc(NULL) == 0);
    CHECK(aos_instruction_arg(NULL, 0) == NULL);
    CHECK(aos_instruction_field(NULL, AOS_FIELD_STDIN) == NULL);
    CHECK(aos_instruction_env_count(NULL) == 0);
    CHECK(aos_instruction_env(NULL, 0) == NULL);
    return 1;
}

static size_t test_arg_out_of_range(void)
{
    aos_instruction *inst = aos_instruction_new();

    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "only") == AOS_INST_OK);
    CHECK(aos_instruction_arg(inst, 0) != NULL);
    CHECK(strcmp(aos_instruction_arg(inst, 0), "only") == 0);
    /* 索引等於 argc 就已經出界，不是最後一個有效索引。 */
    CHECK(aos_instruction_arg(inst, 1) == NULL);
    CHECK(aos_instruction_arg(inst, 100) == NULL);

    aos_instruction_free(inst);
    return 1;
}

static size_t test_null_argument_rejected(void)
{
    aos_instruction *inst = aos_instruction_new();

    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, NULL) == AOS_INST_INVALID_ARGUMENT);
    CHECK(aos_instruction_set_field(inst, AOS_FIELD_CWD, NULL) ==
          AOS_INST_INVALID_ARGUMENT);
    CHECK(aos_instruction_push_env(inst, NULL) == AOS_INST_INVALID_ARGUMENT);
    CHECK(aos_instruction_push_env(NULL, "K=v") == AOS_INST_INVALID_ARGUMENT);
    /* 每次呼叫都必須被拒絕在前面，指令本身要維持沒被改動。 */
    CHECK(aos_instruction_argc(inst) == 0);
    CHECK(aos_instruction_env_count(inst) == 0);

    aos_instruction_free(inst);
    return 1;
}

static size_t test_fields_all_and_out_of_range(void)
{
    aos_instruction *inst = aos_instruction_new();
    size_t i;

    CHECK(inst != NULL);
    /* 未設定的欄位是空字串，不是 NULL；這是呼叫端能安心直接印出來的前提。 */
    CHECK(strcmp(aos_instruction_field(inst, AOS_FIELD_STDIN), "") == 0);

    for (i = 0; i < FIELD_COUNT; ++i) {
        char value[16];
        const char *got;

        snprintf(value, sizeof(value), "field-%zu", i);
        CHECK(aos_instruction_set_field(inst, kAllFields[i], value) ==
              AOS_INST_OK);
        got = aos_instruction_field(inst, kAllFields[i]);
        CHECK(got != NULL);
        CHECK(strcmp(got, value) == 0);
    }

    /*
     * 範圍外的欄位值：field() 回 NULL，set_field() 回 INVALID_ARGUMENT。
     * env 曾經是這個列舉的一員，現在不是了，所以它也走這條路徑。
     */
    CHECK(aos_instruction_field(inst, (aos_inst_field)999) == NULL);
    CHECK(aos_instruction_set_field(inst, (aos_inst_field)999, "x") ==
          AOS_INST_INVALID_ARGUMENT);

    aos_instruction_free(inst);
    return 1;
}

static size_t test_clear(void)
{
    aos_instruction *inst = aos_instruction_new();
    size_t i;

    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "a") == AOS_INST_OK);
    CHECK(aos_instruction_push_arg(inst, "b") == AOS_INST_OK);
    CHECK(aos_instruction_push_env(inst, "K=v") == AOS_INST_OK);
    CHECK(aos_instruction_set_field(inst, AOS_FIELD_CWD, "/tmp") ==
          AOS_INST_OK);

    aos_instruction_clear(inst);
    CHECK(aos_instruction_argc(inst) == 0);
    CHECK(aos_instruction_env_count(inst) == 0);
    for (i = 0; i < FIELD_COUNT; ++i) {
        CHECK(strcmp(aos_instruction_field(inst, kAllFields[i]), "") == 0);
    }

    /* clear(NULL) 沿用 free(NULL) 的「無事發生」慣例。 */
    aos_instruction_clear(NULL);

    aos_instruction_free(inst);
    return 1;
}

/* 建一個帶有引數、env 與全部欄位的指令，寫到暫存檔再讀回，逐項比對。 */
static size_t test_round_trip(void)
{
    static const char *argv[3] = { "a b", "\"quoted value\"", "c" };
    static const char *env[2] = { "PATH=/bin", "MSG=hello world" };
    static const char *values[FIELD_COUNT] = {
        "field-1", "field-2", "field-3", "field-4", "field-5", "field-6"
    };
    aos_instruction *inst = aos_instruction_new();
    aos_instruction *back = aos_instruction_new();
    FILE *f;
    size_t i;

    CHECK(inst != NULL);
    CHECK(back != NULL);

    for (i = 0; i < 3; ++i) {
        CHECK(aos_instruction_push_arg(inst, argv[i]) == AOS_INST_OK);
    }
    for (i = 0; i < 2; ++i) {
        CHECK(aos_instruction_push_env(inst, env[i]) == AOS_INST_OK);
    }
    for (i = 0; i < FIELD_COUNT; ++i) {
        CHECK(aos_instruction_set_field(inst, kAllFields[i], values[i]) ==
              AOS_INST_OK);
    }

    f = must_tmpfile();
    CHECK(aos_instruction_write(f, inst) == AOS_INST_OK);
    /* rewind() 隱含 fflush()，這是在同一個 "+" 串流上從寫切到讀所需要的
     * 唯一動作。 */
    rewind(f);
    CHECK(aos_instruction_read(f, back, aos_inst_record_max_bytes()) ==
          AOS_INST_OK);

    CHECK(aos_instruction_argc(back) == 3);
    for (i = 0; i < 3; ++i) {
        CHECK(strcmp(aos_instruction_arg(back, i), argv[i]) == 0);
    }
    CHECK(aos_instruction_env_count(back) == 2);
    for (i = 0; i < 2; ++i) {
        CHECK(strcmp(aos_instruction_env(back, i), env[i]) == 0);
    }
    /* 索引等於筆數就已經出界，跟 arg() 的規則一致。 */
    CHECK(aos_instruction_env(back, 2) == NULL);
    for (i = 0; i < FIELD_COUNT; ++i) {
        CHECK(strcmp(aos_instruction_field(back, kAllFields[i]),
                     values[i]) == 0);
    }

    fclose(f);
    aos_instruction_free(inst);
    aos_instruction_free(back);
    return 1;
}

static size_t test_read_error_states(void)
{
    aos_instruction *inst = aos_instruction_new();
    FILE *f;
    size_t cases = 0;

    CHECK(inst != NULL);

    /* 空串流：連一筆記錄都還沒開始就結束，是 EOF 而不是 Incomplete。 */
    f = must_tmpfile();
    CHECK(aos_instruction_read(f, inst, aos_inst_record_max_bytes()) ==
          AOS_INST_EOF);
    CHECK(aos_instruction_argc(inst) == 0);
    fclose(f);
    ++cases;

    /* 五行：記錄在八行中途被截斷。 */
    f = must_tmpfile();
    fputs("line0\nline1\nline2\nline3\nline4\n", f);
    rewind(f);
    CHECK(aos_instruction_read(f, inst, aos_inst_record_max_bytes()) ==
          AOS_INST_INCOMPLETE);
    CHECK(aos_instruction_argc(inst) == 0);
    fclose(f);
    ++cases;

    /* argv 行只有換行字元：其餘七行齊全，但沒有任何引數可用。 */
    f = must_tmpfile();
    fputs("\nl1\nl2\nl3\nl4\nl5\nl6\nl7\n", f);
    rewind(f);
    CHECK(aos_instruction_read(f, inst, aos_inst_record_max_bytes()) ==
          AOS_INST_EMPTY_ARGV);
    CHECK(aos_instruction_argc(inst) == 0);
    fclose(f);
    ++cases;

    aos_instruction_free(inst);
    return cases;
}

static size_t test_read_invalid_argument(void)
{
    aos_instruction *inst = aos_instruction_new();
    FILE *f = must_tmpfile();
    size_t cases = 0;

    CHECK(inst != NULL);
    fputs("x\n\n\n\n\n\n\n\n", f);
    rewind(f);

    /* 預算為零：沒有預算就無法界定任何一行的長度上限。 */
    CHECK(aos_instruction_read(f, inst, 0) == AOS_INST_INVALID_ARGUMENT);
    ++cases;

    CHECK(aos_instruction_read(NULL, inst, aos_inst_record_max_bytes()) ==
          AOS_INST_INVALID_ARGUMENT);
    ++cases;

    CHECK(aos_instruction_read(f, NULL, aos_inst_record_max_bytes()) ==
          AOS_INST_INVALID_ARGUMENT);
    ++cases;

    fclose(f);
    aos_instruction_free(inst);
    return cases;
}

static size_t test_record_budget_boundary(void)
{
    /*
     * 八行皆兩個位元組長（不含換行），合計恰好 16 位元組；第 7 行還得是一
     * 筆合法的 KEY=VALUE，"m=" 剛好在兩個位元組內做到。預算檢查發生在把字
     * 元推進緩衝區「之前」，所以剛好等於預算的記錄會成功，少一個位元組的
     * 預算則會在最後一行的第二個字元上被擋下。
     */
    static const char record[] = "ab\ncd\nef\ngh\nij\nkl\nm=\nop\n";
    aos_instruction *inst = aos_instruction_new();
    FILE *f;
    size_t cases = 0;

    CHECK(inst != NULL);

    f = must_tmpfile();
    fputs(record, f);
    rewind(f);
    CHECK(aos_instruction_read(f, inst, 16) == AOS_INST_OK);
    CHECK(aos_instruction_argc(inst) == 1);
    CHECK(strcmp(aos_instruction_arg(inst, 0), "ab") == 0);
    CHECK(strcmp(aos_instruction_field(inst, AOS_FIELD_EXTRA), "op") == 0);
    fclose(f);
    ++cases;

    f = must_tmpfile();
    fputs(record, f);
    rewind(f);
    CHECK(aos_instruction_read(f, inst, 15) == AOS_INST_TOO_LONG);
    CHECK(aos_instruction_argc(inst) == 0);
    fclose(f);
    ++cases;

    aos_instruction_free(inst);
    return cases;
}

static size_t test_write_rejects_tab_and_line_break(void)
{
    aos_instruction *inst;
    FILE *f;
    size_t cases = 0;

    /* 定位字元只在 argv 行有切分意義；混進引數裡會跟分隔符號混淆。 */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "ok") == AOS_INST_OK);
    CHECK(aos_instruction_push_arg(inst, "has\ttab") == AOS_INST_OK);
    f = must_tmpfile();
    CHECK(aos_instruction_write(f, inst) == AOS_INST_ARGUMENT_CONTAINS_TAB);
    /* 整筆記錄先驗證再寫入，被拒絕時必須一個位元組都沒寫出去。 */
    CHECK(ftell(f) == 0);
    fclose(f);
    aos_instruction_free(inst);
    ++cases;

    /* 換行會跟行尾終止字元混淆，任何一個欄位都不能含有它。 */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "prog") == AOS_INST_OK);
    CHECK(aos_instruction_set_field(inst, AOS_FIELD_CWD, "value\n") ==
          AOS_INST_OK);
    f = must_tmpfile();
    CHECK(aos_instruction_write(f, inst) ==
          AOS_INST_FIELD_CONTAINS_LINE_BREAK);
    CHECK(ftell(f) == 0);
    fclose(f);
    aos_instruction_free(inst);
    ++cases;

    return cases;
}

/*
 * 標準的兩段式呼叫：第一次不給緩衝區，只問需要多少位元組；size 是 0 時
 * buffer 甚至可以是 NULL，因為函式在檢查大小之前不會碰它。
 */
static size_t test_write_buffer_sizing_pattern(void)
{
    aos_instruction *inst = make_sample_instruction();
    size_t needed = 999;
    char *buffer;

    CHECK(aos_instruction_write_buffer(inst, NULL, 0, &needed) ==
          AOS_INST_BUFFER_TOO_SMALL);
    CHECK(needed > 0);

    buffer = (char *)malloc(needed + 1);
    CHECK(buffer != NULL);
    CHECK(aos_instruction_write_buffer(inst, buffer, needed + 1, &needed) ==
          AOS_INST_OK);
    /* 結果必須以 NUL 結尾，且長度剛好等於 *needed。 */
    CHECK(strlen(buffer) == needed);

    free(buffer);
    aos_instruction_free(inst);
    return 1;
}

/*
 * 真正要緊的性質：兩條輸出路徑寫出來的位元組必須完全一樣，不能只是「看起
 * 來差不多」。拿 aos_instruction_write 落到暫存檔的內容逐位元組比對。
 */
static size_t test_write_buffer_matches_write(void)
{
    aos_instruction *inst = make_sample_instruction();
    size_t needed = 0;
    char *buffer;
    char *from_file;
    FILE *f;

    CHECK(aos_instruction_write_buffer(inst, NULL, 0, &needed) ==
          AOS_INST_BUFFER_TOO_SMALL);

    buffer = (char *)malloc(needed + 1);
    CHECK(buffer != NULL);
    CHECK(aos_instruction_write_buffer(inst, buffer, needed + 1, &needed) ==
          AOS_INST_OK);

    f = must_tmpfile();
    CHECK(aos_instruction_write(f, inst) == AOS_INST_OK);
    rewind(f);

    from_file = (char *)malloc(needed);
    CHECK(from_file != NULL);
    CHECK(fread(from_file, 1, needed, f) == needed);
    /* 兩條路徑的長度必須完全一致，串流應該正好在這裡耗盡。 */
    CHECK(fgetc(f) == EOF);

    CHECK(memcmp(buffer, from_file, needed) == 0);

    free(from_file);
    free(buffer);
    fclose(f);
    aos_instruction_free(inst);
    return 1;
}

static size_t test_write_buffer_boundary(void)
{
    aos_instruction *inst = make_sample_instruction();
    size_t needed = 0;
    char *buffer;
    size_t cases = 0;

    CHECK(aos_instruction_write_buffer(inst, NULL, 0, &needed) ==
          AOS_INST_BUFFER_TOO_SMALL);
    CHECK(needed > 0);

    buffer = (char *)malloc(needed + 1);
    CHECK(buffer != NULL);

    {
        size_t needed2 = 0;

        /* size 剛好等於內容長度：容不下結尾的 NUL，仍然太小。 */
        CHECK(aos_instruction_write_buffer(inst, buffer, needed, &needed2) ==
              AOS_INST_BUFFER_TOO_SMALL);
        CHECK(needed2 == needed);
        ++cases;
    }
    {
        size_t needed2 = 0;

        /* size 多一個位元組，剛好夠放 NUL。 */
        CHECK(aos_instruction_write_buffer(inst, buffer, needed + 1,
                                           &needed2) == AOS_INST_OK);
        CHECK(needed2 == needed);
        ++cases;
    }

    free(buffer);
    aos_instruction_free(inst);
    return cases;
}

static size_t test_write_buffer_validation_failures(void)
{
    aos_instruction *inst;
    char buffer[64];
    size_t needed;
    size_t cases = 0;

    /* 引數含定位字元：驗證在量長度之前就先擋下，*needed 停在 0。 */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "has\ttab") == AOS_INST_OK);
    needed = 999;
    CHECK(aos_instruction_write_buffer(inst, buffer, sizeof(buffer),
                                       &needed) ==
          AOS_INST_ARGUMENT_CONTAINS_TAB);
    CHECK(needed == 0);
    aos_instruction_free(inst);
    ++cases;

    /* 空 argv：同樣是驗證失敗，不是「內容剛好序列化成空字串」。 */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    needed = 999;
    CHECK(aos_instruction_write_buffer(inst, buffer, sizeof(buffer),
                                       &needed) == AOS_INST_EMPTY_ARGV);
    CHECK(needed == 0);
    aos_instruction_free(inst);
    ++cases;

    return cases;
}

static size_t test_write_buffer_null_handling(void)
{
    aos_instruction *inst;
    char buffer[256];
    size_t needed = 999;
    size_t cases = 0;

    /* inst 是 NULL：要在動到別的東西之前就回報錯誤；needed 仍然被清空。 */
    CHECK(aos_instruction_write_buffer(NULL, buffer, sizeof(buffer),
                                       &needed) ==
          AOS_INST_INVALID_ARGUMENT);
    CHECK(needed == 0);
    ++cases;

    /* needed 是 NULL 是合法用法：呼叫端已經知道緩衝區夠大，不需要確切
     * 長度，函式不能因此當機。 */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "prog") == AOS_INST_OK);
    CHECK(aos_instruction_write_buffer(inst, buffer, sizeof(buffer), NULL) ==
          AOS_INST_OK);
    aos_instruction_free(inst);
    ++cases;

    return cases;
}

/* 緩衝區本身沒有串流可讀，寫進暫存檔是唯一能餵回 aos_instruction_read
 * 的方式；藉此確認 write_buffer 產生的位元組是一筆真正可解析的記錄。 */
static size_t test_write_buffer_round_trip_through_read(void)
{
    aos_instruction *inst = make_sample_instruction();
    aos_instruction *back = aos_instruction_new();
    size_t needed = 0;
    char *buffer;
    FILE *f;
    size_t i;

    CHECK(back != NULL);
    CHECK(aos_instruction_write_buffer(inst, NULL, 0, &needed) ==
          AOS_INST_BUFFER_TOO_SMALL);
    buffer = (char *)malloc(needed + 1);
    CHECK(buffer != NULL);
    CHECK(aos_instruction_write_buffer(inst, buffer, needed + 1, &needed) ==
          AOS_INST_OK);

    f = must_tmpfile();
    CHECK(fwrite(buffer, 1, needed, f) == needed);
    rewind(f);
    CHECK(aos_instruction_read(f, back, aos_inst_record_max_bytes()) ==
          AOS_INST_OK);

    CHECK(aos_instruction_argc(back) == aos_instruction_argc(inst));
    for (i = 0; i < aos_instruction_argc(inst); ++i) {
        CHECK(strcmp(aos_instruction_arg(back, i),
                     aos_instruction_arg(inst, i)) == 0);
    }
    CHECK(aos_instruction_env_count(back) == aos_instruction_env_count(inst));
    for (i = 0; i < aos_instruction_env_count(inst); ++i) {
        CHECK(strcmp(aos_instruction_env(back, i),
                     aos_instruction_env(inst, i)) == 0);
    }
    for (i = 0; i < FIELD_COUNT; ++i) {
        CHECK(strcmp(aos_instruction_field(back, kAllFields[i]),
                     aos_instruction_field(inst, kAllFields[i])) == 0);
    }

    fclose(f);
    free(buffer);
    aos_instruction_free(inst);
    aos_instruction_free(back);
    return 1;
}

/*
 * 沒有專用的附加函式：write 本身就是往串流目前的位置寫，重複呼叫兩次自
 * 然就是附加兩筆記錄。這是文件裡寫明的用法，這裡把它真正測過一遍。
 */
static size_t test_append_two_records_via_write(void)
{
    aos_instruction *first = aos_instruction_new();
    aos_instruction *second = aos_instruction_new();
    aos_instruction *inst = aos_instruction_new();
    FILE *f;

    CHECK(first != NULL);
    CHECK(second != NULL);
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(first, "one") == AOS_INST_OK);
    CHECK(aos_instruction_set_field(first, AOS_FIELD_CWD, "/first") ==
          AOS_INST_OK);
    CHECK(aos_instruction_push_arg(second, "two") == AOS_INST_OK);
    CHECK(aos_instruction_push_arg(second, "b") == AOS_INST_OK);
    CHECK(aos_instruction_set_field(second, AOS_FIELD_CWD, "/second") ==
          AOS_INST_OK);

    f = must_tmpfile();
    CHECK(aos_instruction_write(f, first) == AOS_INST_OK);
    CHECK(aos_instruction_write(f, second) == AOS_INST_OK);
    rewind(f);

    CHECK(aos_instruction_read(f, inst, aos_inst_record_max_bytes()) ==
          AOS_INST_OK);
    CHECK(aos_instruction_argc(inst) == 1);
    CHECK(strcmp(aos_instruction_arg(inst, 0), "one") == 0);
    CHECK(strcmp(aos_instruction_field(inst, AOS_FIELD_CWD), "/first") == 0);

    CHECK(aos_instruction_read(f, inst, aos_inst_record_max_bytes()) ==
          AOS_INST_OK);
    CHECK(aos_instruction_argc(inst) == 2);
    CHECK(strcmp(aos_instruction_arg(inst, 0), "two") == 0);
    CHECK(strcmp(aos_instruction_arg(inst, 1), "b") == 0);
    CHECK(strcmp(aos_instruction_field(inst, AOS_FIELD_CWD), "/second") == 0);

    CHECK(aos_instruction_read(f, inst, aos_inst_record_max_bytes()) ==
          AOS_INST_EOF);
    CHECK(aos_instruction_argc(inst) == 0);

    fclose(f);
    aos_instruction_free(first);
    aos_instruction_free(second);
    aos_instruction_free(inst);
    return 1;
}

/*
 * env 是清單而不是字串，所以它的錯誤只在寫入時才看得到：push_env 收下任何
 * 位元組，write 才判斷這一筆能不能讀回來。
 */
static size_t test_env_write_validation(void)
{
    static const char *bad[3] = { "NOEQUALS", "=value", "A=has\ttab" };
    size_t cases = 0;
    size_t i;

    for (i = 0; i < 3; ++i) {
        aos_instruction *inst = aos_instruction_new();
        FILE *f = must_tmpfile();

        CHECK(inst != NULL);
        CHECK(aos_instruction_push_arg(inst, "prog") == AOS_INST_OK);
        /* 收下時不驗，所以這一步是 OK。 */
        CHECK(aos_instruction_push_env(inst, bad[i]) == AOS_INST_OK);
        CHECK(aos_instruction_write(f, inst) ==
              AOS_INST_ENV_ENTRY_MALFORMED);
        fclose(f);
        aos_instruction_free(inst);
        ++cases;
    }
    return cases;
}

static size_t test_argv_and_record_limits(void)
{
    const size_t argv_max = aos_inst_argv_max();
    const size_t env_max = aos_inst_env_max();
    const size_t record_max = aos_inst_record_max_bytes();
    aos_instruction *inst = aos_instruction_new();
    FILE *f;
    size_t i;

    /* 三個限制都是「函式庫實際編譯出來的值」，呼叫端不該自己硬編一個。 */
    CHECK(argv_max > 0);
    CHECK(env_max > 0);
    CHECK(record_max > 0);
    CHECK(inst != NULL);

    for (i = 0; i < argv_max + 1; ++i) {
        char value[24];

        snprintf(value, sizeof(value), "a%zu", i);
        CHECK(aos_instruction_push_arg(inst, value) == AOS_INST_OK);
    }

    f = must_tmpfile();
    CHECK(aos_instruction_write(f, inst) == AOS_INST_TOO_MANY_ARGS);
    fclose(f);
    aos_instruction_free(inst);

    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "prog") == AOS_INST_OK);
    for (i = 0; i < env_max + 1; ++i) {
        char value[24];

        snprintf(value, sizeof(value), "K%zu=v", i);
        CHECK(aos_instruction_push_env(inst, value) == AOS_INST_OK);
    }
    CHECK(aos_instruction_env_count(inst) == env_max + 1);

    f = must_tmpfile();
    CHECK(aos_instruction_write(f, inst) == AOS_INST_TOO_MANY_ENV);
    fclose(f);

    aos_instruction_free(inst);
    return 1;
}

static size_t test_inst_state_strings(void)
{
    static const aos_inst_state states[] = {
        AOS_INST_OK,
        AOS_INST_INVALID_ARGUMENT,
        AOS_INST_EOF,
        AOS_INST_INCOMPLETE,
        AOS_INST_TOO_LONG,
        AOS_INST_READ_ERROR,
        AOS_INST_EMPTY_ARGV,
        AOS_INST_TOO_MANY_ARGS,
        AOS_INST_ARGUMENT_CONTAINS_TAB,
        AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK,
        AOS_INST_FIELD_CONTAINS_LINE_BREAK,
        AOS_INST_WRITE_ERROR,
        AOS_INST_ENV_ENTRY_MALFORMED,
        AOS_INST_TOO_MANY_ENV,
        /* 只存在於 C 介面：C++ 端沒有對應值可以轉，字串在這裡才第一次
         * 出現。 */
        AOS_INST_ALLOC_FAILED,
        /* 同樣只存在於 C 介面，是 aos_instruction_write_buffer 專用的
         * 狀態。 */
        AOS_INST_BUFFER_TOO_SMALL
    };
    size_t i;
    const char *text;

    for (i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        text = aos_inst_state_string(states[i]);
        CHECK(text != NULL);
        CHECK(strlen(text) > 0);
    }

    /* 範圍外的值走的是 switch 之外的預設回傳路徑，不該回傳 NULL。 */
    text = aos_inst_state_string((aos_inst_state)999);
    CHECK(text != NULL);
    CHECK(strlen(text) > 0);

    return 1;
}

static size_t test_exec_state_strings(void)
{
    static const aos_exec_state states[] = {
        AOS_EXEC_OK,
        AOS_EXEC_INVALID_ARGUMENT,
        AOS_EXEC_SPAWN_FAILED,
        AOS_EXEC_WAIT_FAILED,
        AOS_EXEC_EXIT_WRITE_FAILED,
        /* 同樣只存在於 C 介面。 */
        AOS_EXEC_ALLOC_FAILED
    };
    size_t i;
    const char *text;

    for (i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        text = aos_exec_state_string(states[i]);
        CHECK(text != NULL);
        CHECK(strlen(text) > 0);
    }

    text = aos_exec_state_string((aos_exec_state)999);
    CHECK(text != NULL);
    CHECK(strlen(text) > 0);

    return 1;
}

static size_t test_version_string(void)
{
    const char *version = aos_version_string();

    CHECK(version != NULL);
    CHECK(strlen(version) > 0);
    return 1;
}

/*
 * 讀出整個檔案的內容，用來核對 stdout / exit 兩個欄位實際落地的內容。
 * 呼叫端要負責 free() 回傳的緩衝區。
 */
static char *slurp_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;

    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    size = ftell(f);
    CHECK(size >= 0);
    CHECK(fseek(f, 0, SEEK_SET) == 0);

    buf = (char *)malloc((size_t)size + 1);
    CHECK(buf != NULL);
    if (size > 0) {
        CHECK(fread(buf, 1, (size_t)size, f) == (size_t)size);
    }
    buf[size] = '\0';

    fclose(f);
    return buf;
}

/* 透過公開 C ABI 實際跑子行程，證明 execute 這條路徑跨得過這道邊界。 */
static size_t test_execute(void)
{
    char dir_template[] = "/tmp/aos_c_capi_test_XXXXXX";
    char out_path[600];
    char exit_path[600];
    char *dir;
    aos_instruction *inst;
    aos_exec_result result;
    char *content;
    size_t cases = 0;

    dir = mkdtemp(dir_template);
    CHECK(dir != NULL);
    snprintf(out_path, sizeof(out_path), "%s/out", dir);
    snprintf(exit_path, sizeof(exit_path), "%s/exit", dir);

    /* echo：核對 stdout 檔案與 exit 檔案兩邊落地的內容。 */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "echo") == AOS_INST_OK);
    CHECK(aos_instruction_push_arg(inst, "hello") == AOS_INST_OK);
    CHECK(aos_instruction_set_field(inst, AOS_FIELD_STDOUT, out_path) ==
          AOS_INST_OK);
    CHECK(aos_instruction_set_field(inst, AOS_FIELD_EXIT, exit_path) ==
          AOS_INST_OK);
    CHECK(aos_instruction_execute(inst, &result) == AOS_EXEC_OK);
    CHECK(result.status == 0);
    CHECK(result.signalled == 0);
    aos_instruction_free(inst);

    content = slurp_file(out_path);
    CHECK(strcmp(content, "hello\n") == 0);
    free(content);
    content = slurp_file(exit_path);
    CHECK(strcmp(content, "0\n") == 0);
    free(content);
    remove(out_path);
    remove(exit_path);
    ++cases;

    /*
     * 不存在的指令：跟 shell 一樣是一筆跑完並產出 127 的記錄，而且 exit
     * 檔照樣要被寫出來 —— 它代表「這筆做完了」，不是「做對了」。
     */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(
              inst, "aos-c-capi-test-no-such-command-xyz") == AOS_INST_OK);
    CHECK(aos_instruction_set_field(inst, AOS_FIELD_EXIT, exit_path) ==
          AOS_INST_OK);
    CHECK(aos_instruction_execute(inst, &result) == AOS_EXEC_OK);
    CHECK(result.status == 127);
    aos_instruction_free(inst);

    content = slurp_file(exit_path);
    CHECK(strcmp(content, "127\n") == 0);
    free(content);
    remove(exit_path);
    ++cases;

    /* 子行程正常結束但結束碼非零。 */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "sh") == AOS_INST_OK);
    CHECK(aos_instruction_push_arg(inst, "-c") == AOS_INST_OK);
    CHECK(aos_instruction_push_arg(inst, "exit 3") == AOS_INST_OK);
    CHECK(aos_instruction_execute(inst, &result) == AOS_EXEC_OK);
    CHECK(result.status == 3);
    aos_instruction_free(inst);
    ++cases;

    /* result 可以是 NULL：呼叫端只在乎狀態時，不該被強迫提供輸出空間。 */
    inst = aos_instruction_new();
    CHECK(inst != NULL);
    CHECK(aos_instruction_push_arg(inst, "true") == AOS_INST_OK);
    CHECK(aos_instruction_execute(inst, NULL) == AOS_EXEC_OK);
    aos_instruction_free(inst);
    ++cases;

    /* inst 是 NULL：不能當機，且 result 要被清成已知狀態而不是原樣保留
     * 呼叫端傳進來的垃圾值。 */
    result.status = -1;
    result.signalled = -1;
    result.error = -1;
    CHECK(aos_instruction_execute(NULL, &result) == AOS_EXEC_INVALID_ARGUMENT);
    CHECK(result.status == 0);
    CHECK(result.signalled == 0);
    CHECK(result.error == 0);
    ++cases;

    remove(dir);
    return cases;
}

size_t run_capi_tests(void)
{
    size_t count = 0;

    count += test_lifecycle();
    count += test_null_accessors();
    count += test_arg_out_of_range();
    count += test_null_argument_rejected();
    count += test_fields_all_and_out_of_range();
    count += test_clear();
    count += test_round_trip();
    count += test_read_error_states();
    count += test_read_invalid_argument();
    count += test_record_budget_boundary();
    count += test_write_rejects_tab_and_line_break();
    count += test_write_buffer_sizing_pattern();
    count += test_write_buffer_matches_write();
    count += test_write_buffer_boundary();
    count += test_write_buffer_validation_failures();
    count += test_write_buffer_null_handling();
    count += test_write_buffer_round_trip_through_read();
    count += test_append_two_records_via_write();
    count += test_env_write_validation();
    count += test_argv_and_record_limits();
    count += test_inst_state_strings();
    count += test_exec_state_strings();
    count += test_version_string();
    count += test_execute();

    return count;
}
