#ifndef AOS_AOS_H
#define AOS_AOS_H

#include "aos/export.h"

#include <stddef.h>
#include <stdio.h>

/*
 * The public aos interface.
 *
 * This is a C header on purpose. The implementation is C++11, but a shared
 * library that exports std::string and std::vector is really "a library for
 * programs built with exactly this compiler, standard library and ABI
 * setting". Everything crossing this boundary is a C type, an opaque
 * pointer, or a plain enum, so a consumer needs nothing but a C compiler
 * and a linker.
 *
 * No function here throws, propagates a C++ exception, or aborts on
 * allocation failure: running out of memory is reported as
 * AOS_INST_ALLOC_FAILED or a NULL return.
 *
 * An instruction is nine lines: eight field lines, one field per line,
 *   argv, stdin, stdout, stderr, exit, cwd, env, extra,
 * followed by a ninth line that is always empty and separates one record
 * from the next. The argv and env lines are tab-separated with no quoting or
 * escaping; spaces, quotes and backslashes are ordinary characters, and
 * adjacent tabs preserve empty arguments. The env line carries the
 * environment itself, one KEY=VALUE per entry, and an empty env line is an
 * empty list, which is legal; an empty argv line is not. Every line,
 * including the eighth field line and the blank separator, ends with a
 * newline. The separator makes a misaligned stream detectable: a record
 * missing or gaining a line pushes a non-empty data line into the separator
 * position, which the reader rejects rather than silently running the wrong
 * command. Both LF and CRLF are accepted on input; the writer always emits
 * LF.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define AOS_VERSION_MAJOR 0
#define AOS_VERSION_MINOR 1
#define AOS_VERSION_PATCH 0

/*
 * Result of reading, or of validating and writing, one instruction.
 *
 * The values are part of the ABI. New states may only be appended;
 * reordering silently changes the meaning of a stored or transmitted code.
 */
typedef enum aos_inst_state {
    AOS_INST_OK = 0,
    AOS_INST_INVALID_ARGUMENT = 1,
    /* 讀取：串流結束前沒有任何記錄開始。 */
    AOS_INST_EOF = 2,
    /* 讀取：串流在一筆記錄的中途結束。 */
    AOS_INST_INCOMPLETE = 3,
    /* 讀取：這筆記錄超出呼叫端給的位元組預算。 */
    AOS_INST_TOO_LONG = 4,
    AOS_INST_READ_ERROR = 5,
    /* 讀寫共用。 */
    AOS_INST_EMPTY_ARGV = 6,
    AOS_INST_TOO_MANY_ARGS = 7,
    /* 寫入：某個值含有格式用作分隔符號的位元組。 */
    AOS_INST_ARGUMENT_CONTAINS_TAB = 8,
    AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK = 9,
    AOS_INST_FIELD_CONTAINS_LINE_BREAK = 10,
    AOS_INST_WRITE_ERROR = 11,
    /* 讀寫共用：env 的某一筆不是合法的 KEY=VALUE。 */
    AOS_INST_ENV_ENTRY_MALFORMED = 12,
    /* 讀寫共用：env 的筆數超過 aos_inst_env_max()。 */
    AOS_INST_TOO_MANY_ENV = 13,
    /*
     * 只存在於 C 介面。實作內部的配置失敗是 std::bad_alloc，但例外不能穿
     * 過這道邊界，所以在這裡被翻譯成一個狀態。
     */
    AOS_INST_ALLOC_FAILED = 14,
    /* 同樣只存在於 C 介面：aos_instruction_write_buffer 的緩衝區不夠大。 */
    AOS_INST_BUFFER_TOO_SMALL = 15,
    /* 讀取：八行欄位之後那一行不是空的，代表記錄錯位。 */
    AOS_INST_MISSING_SEPARATOR = 16
} aos_inst_state;

/*
 * The single-string fields, in the order they are serialized. argv and env
 * are lists rather than strings, so they are not here: they have their own
 * count/get/push functions.
 */
typedef enum aos_inst_field {
    AOS_FIELD_STDIN = 0,
    AOS_FIELD_STDOUT = 1,
    AOS_FIELD_STDERR = 2,
    AOS_FIELD_EXIT = 3,
    AOS_FIELD_CWD = 4,
    AOS_FIELD_EXTRA = 5
} aos_inst_field;

/*
 * Result of trying to run one instruction. See aos_instruction_execute.
 *
 * There are few failures here on purpose: a command that could not be
 * started is not one of them. It is an exit status, 126 or 127, exactly as
 * in a shell.
 */
typedef enum aos_exec_state {
    AOS_EXEC_OK = 0,
    AOS_EXEC_INVALID_ARGUMENT = 1,
    /* fork 失敗：唯一連子行程都不存在的啟動錯誤。 */
    AOS_EXEC_SPAWN_FAILED = 2,
    AOS_EXEC_WAIT_FAILED = 3,
    AOS_EXEC_EXIT_WRITE_FAILED = 4,
    /*
     * 5 是退休的編號，曾經是 AOS_EXEC_PLATFORM_UNSUPPORTED。專案已經只支援
     * POSIX，這個狀態不可能再出現；空著不補是刻意的，免得舊的呼叫端拿到 5
     * 時被當成別的意思。
     */
    AOS_EXEC_ALLOC_FAILED = 6
} aos_exec_state;

/*
 * How the child ended. Only meaningful when aos_instruction_execute
 * returned AOS_EXEC_OK. This is a plain struct by design: it is copied
 * across the boundary rather than owned by the library.
 */
typedef struct aos_exec_result {
    /*
     * 正常結束時是結束碼；被訊號終止時是 128 + 訊號編號；子行程沒能起來時
     * 是 126（佈置失敗）或 127（找不到指令）。
     */
    int status;
    /* 非零代表 status 來自訊號而非 exit()。 */
    int signalled;
    /* fork 或 wait 失敗時作業系統回報的 errno，否則為 0。 */
    int error;
} aos_exec_result;

/*
 * An instruction. Opaque: its layout is not part of the ABI, so fields may
 * be added to the implementation without breaking a compiled consumer.
 */
typedef struct aos_instruction aos_instruction;

/* Create an empty instruction, or NULL if memory ran out. */
AOS_API aos_instruction *aos_instruction_new(void);

/* Release an instruction. Does nothing when inst is NULL. */
AOS_API void aos_instruction_free(aos_instruction *inst);

/* Return the instruction to its empty state. */
AOS_API void aos_instruction_clear(aos_instruction *inst);

/* Number of arguments. Zero for an empty or failed-to-read instruction. */
AOS_API size_t aos_instruction_argc(const aos_instruction *inst);

/*
 * Argument at index, or NULL when index is out of range. The pointer is
 * owned by the instruction and is valid until the next call that modifies
 * it -- a read, a clear, a push, or a free.
 */
AOS_API const char *aos_instruction_arg(const aos_instruction *inst,
                                        size_t index);

/*
 * One of the single-string fields, never NULL for a valid field; an unset
 * field is the empty string. Same lifetime as aos_instruction_arg.
 */
AOS_API const char *aos_instruction_field(const aos_instruction *inst,
                                          aos_inst_field field);

/*
 * The environment, entry by entry. Zero entries means the child inherits
 * this process's environment unchanged; any entries extend it -- each
 * KEY=VALUE overrides a matching name, adds an unmatched one, and leaves the
 * rest in place, with a later duplicate winning over an earlier one.
 * aos_instruction_env returns NULL when index is out of range, and borrows
 * the instruction's storage exactly as aos_instruction_arg does.
 */
AOS_API size_t aos_instruction_env_count(const aos_instruction *inst);
AOS_API const char *aos_instruction_env(const aos_instruction *inst,
                                        size_t index);

/*
 * Append an argument, or set one of the single-string fields. value is
 * copied.
 * Return AOS_INST_OK, AOS_INST_INVALID_ARGUMENT for a NULL pointer or an
 * unknown field, or AOS_INST_ALLOC_FAILED.
 */
AOS_API aos_inst_state aos_instruction_push_arg(aos_instruction *inst,
                                                const char *value);
AOS_API aos_inst_state aos_instruction_set_field(aos_instruction *inst,
                                                 aos_inst_field field,
                                                 const char *value);

/*
 * Append one environment entry, which must be a whole "KEY=VALUE" string.
 *
 * It takes the entry, not a key and a value, because that is what the
 * instruction stores and what the child is handed. A caller that thinks in
 * keys and values already has a map of its own; flattening it is one
 * snprintf, and doing it here would mean this library owning a merge policy
 * for duplicate keys that nothing has asked it to have.
 *
 * The entry is validated on write, not here, so that building an
 * instruction never fails for a reason the caller cannot see yet.
 */
AOS_API aos_inst_state aos_instruction_push_env(aos_instruction *inst,
                                                const char *entry);

/*
 * Read the next record from stream.
 *
 * Returns AOS_INST_EOF when the stream ended cleanly between records. On
 * that and on every other failure inst is left empty, so a half-parsed
 * record can never be mistaken for a whole one and the previous record
 * never survives a failed read.
 *
 * max_record_bytes bounds the eight field lines' combined length as they
 * arrive, excluding the newline that ends each of them and the blank
 * separator, and must be positive:
 * without it one malformed line would allocate without bound, and an
 * instruction file is a trust boundary. Pass aos_inst_record_max_bytes()
 * for the library's default. Exceeding it is AOS_INST_TOO_LONG.
 *
 * The bytes consumed so far are gone on failure. A stream cannot be
 * rewound, so AOS_INST_INCOMPLETE and AOS_INST_TOO_LONG end the run rather
 * than inviting a retry.
 */
AOS_API aos_inst_state aos_instruction_read(FILE *stream,
                                            aos_instruction *inst,
                                            size_t max_record_bytes);

/*
 * 從記憶體裡的一段位元組讀下一筆,是 aos_instruction_read 的 FILE-free 版本,
 * 好讓不方便造出 FILE* 的呼叫端(尤其是其他語言的綁定)也能讀。
 *
 * 從 buffer[0] 開始讀一筆,把實際吃掉的位元組數寫進 *consumed —— 呼叫端據此
 * 前進讀下一筆。回傳值與 aos_instruction_read 完全一樣(含 AOS_INST_EOF)。
 *
 * consumed 可以傳 NULL。任何失敗(含 EOF)都會讓 inst 變成空的,規則同 read。
 */
AOS_API aos_inst_state aos_instruction_read_buffer(const char *buffer,
                                                   size_t size,
                                                   aos_instruction *inst,
                                                   size_t max_record_bytes,
                                                   size_t *consumed);

/*
 * Write one instruction as nine lines -- eight field lines plus a trailing
 * blank separator, each ending in LF -- so repeated calls append records
 * that aos_instruction_read consumes. The whole record
 * is validated before the first byte is written, so a rejected instruction
 * leaves the stream untouched. A write that fails part-way through may
 * still have emitted a partial record.
 */
AOS_API aos_inst_state aos_instruction_write(FILE *stream,
                                             const aos_instruction *inst);

/*
 * Serialize one instruction into a caller-owned buffer instead of a stream,
 * for callers that want the bytes in memory -- to put in a packet, hand to
 * another library, or write with something that is not a FILE *.
 *
 * The usual two-call pattern applies. Ask for the size first:
 *
 *     size_t needed = 0;
 *     aos_instruction_write_buffer(inst, NULL, 0, &needed);
 *     char *buf = malloc(needed + 1);
 *     aos_instruction_write_buffer(inst, buf, needed + 1, &needed);
 *
 * *needed receives the serialized length excluding the terminating NUL, and
 * is set even when the buffer is too small, so a rejected first call is how
 * you learn the size. The buffer must have room for that NUL, so size must
 * exceed *needed; too small is AOS_INST_BUFFER_TOO_SMALL and nothing is
 * written. The record is validated exactly as aos_instruction_write does,
 * and a validation failure leaves *needed at zero.
 *
 * The result is NUL-terminated for convenience, but the record itself never
 * contains a NUL, so the length is equally usable.
 */
AOS_API aos_inst_state aos_instruction_write_buffer(const aos_instruction *inst,
                                                    char *buffer, size_t size,
                                                    size_t *needed);

/*
 * Run one instruction to completion and report how its process ended.
 *
 * Field semantics, none of which the format itself defines. These are
 * choices; changing one is a compatibility break, not a bug fix:
 *
 *   argv        argv[0] is looked up on PATH, as execvp does.
 *   stdin       empty inherits the caller's handle; otherwise opened for
 *   stdout      reading, or created and truncated for writing. Truncating
 *   stderr      matches a shell's `>` rather than `>>`.
 *   exit        empty discards the status; otherwise the file is truncated
 *               and the decimal status plus a newline is written to it,
 *               whether the command succeeded, failed, or never started.
 *               It marks the instruction as finished, not as correct.
 *   cwd         empty inherits the caller's working directory.
 *   env         empty inherits the caller's environment entirely.
 *               Otherwise the entries *extend* it: each is applied with
 *               setenv, overriding a matching name, adding an unmatched one,
 *               and leaving every other inherited variable in place. A later
 *               duplicate key wins over an earlier one.
 *   extra       ignored.
 *
 * The status, following the shell's conventions:
 *
 *   0-255       the child's own exit code.
 *   128 + n     the child was killed by signal n.
 *   126         the child could not be set up: a redirection would not
 *               open, or cwd would not change.
 *   127         execvp failed: no such command, or it is not executable.
 *
 * 126 and 127 are indistinguishable from a child that genuinely exits with
 * those codes, exactly as in a shell.
 *
 * result may be NULL if only the state is wanted. Process spawning is
 * POSIX-only; the library does not build on any other platform.
 */
AOS_API aos_exec_state aos_instruction_execute(aos_instruction *inst,
                                               aos_exec_result *result);

/*
 * Limits compiled into this library. Call these rather than hard-coding a
 * constant: the value the library was built with is the one that governs,
 * and a header a consumer edited is not it.
 */
AOS_API size_t aos_inst_argv_max(void);
AOS_API size_t aos_inst_env_max(void);
AOS_API size_t aos_inst_record_max_bytes(void);

/* Static, human-readable descriptions. Never NULL, even for a bad value. */
AOS_API const char *aos_inst_state_string(aos_inst_state state);
AOS_API const char *aos_exec_state_string(aos_exec_state state);

/* Static version string of the library, as "major.minor.patch". */
AOS_API const char *aos_version_string(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif
