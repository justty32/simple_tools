#ifndef AOS_INST_HPP
#define AOS_INST_HPP

#include "aos/export.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

/*
 * One instruction: a serialized process spawn, and the reader and writer
 * that move it between a stream and memory.
 *
 * An instruction is nine lines: eight field lines, one per field,
 *   argv, stdin, stdout, stderr, exit, cwd, env, extra,
 * followed by a ninth line that is always empty and separates one record
 * from the next. The argv and env lines are tab-separated with no quoting or
 * escaping; spaces, quotes and backslashes are ordinary characters, and
 * adjacent tabs preserve empty arguments. Every line, including the eighth
 * field line and the blank separator, ends with a newline. The separator
 * makes a misaligned stream detectable: a record missing or gaining a line
 * pushes a non-empty data line into the separator position, which the reader
 * rejects rather than silently running the wrong command. Both LF and CRLF
 * are accepted on input; the writer always emits LF.
 *
 * The env line carries the environment itself, one KEY=VALUE per tab-
 * separated entry, rather than a path to read it from. An empty env line is
 * an empty environment list, which is legal and means "inherit"; an empty
 * argv line is not.
 *
 * Reading is stream-based and consumes exactly one record per call, so
 * pipes and std::cin work and memory is bounded by the longest record
 * rather than by the size of the input. Random access across every record
 * in a file is what that trades away.
 *
 * Neither reader nor writer throws for anything it can report: exhausted
 * input, malformed records and stream failures all come back as an
 * InstState. Running out of memory is the exception, in both senses --
 * std::string and std::vector throw std::bad_alloc, and there is no
 * InstState for it.
 */
namespace aos {

/*
 * 一筆指令可攜帶的最大引數數量。讀取端與寫入端共用，因此寫入端接受的每一
 * 筆記錄，讀取端都能解析回來。需要與函式庫內編譯的數值一致時，請呼叫
 * inst_argv_max() 而非直接讀取這個常數。
 */
constexpr std::size_t kInstArgvMax = 256;

/*
 * 一筆指令可攜帶的最大環境變數數量。與 argv 同一個數量級，理由也相同：
 * 這是不受信任輸入的界線，而不是對真實用量的預測。
 */
constexpr std::size_t kInstEnvMax = 256;

/*
 * 一筆序列化記錄的欄位行數。一筆記錄是這八行欄位再加一行固定為空的分隔行，
 * 共九行；這個常數只算欄位行(它驅動 lines[8] 陣列與八次欄位讀取迴圈)。
 * 更動它就是更動格式。
 */
constexpr std::size_t kInstLineCount = 8;

/*
 * 單筆記錄的預設位元組預算，計算八行欄位的內容長度，不含行尾字元，也不含
 * 本該為空的分隔行。這只是預設值，不是 API 的上限：任何正值都可以傳給
 * read_instruction。
 */
constexpr std::size_t kInstRecordMaxBytes = 1024 * 1024;

/*
 * Result of reading, or of validating and writing, one instruction.
 *
 * Consumers compile against the numeric values, so new states may only be
 * appended; reordering silently changes the meaning of a stored code.
 */
enum class InstState {
    Ok = 0,
    InvalidArgument,
    /* 讀取：串流結束前沒有任何記錄開始。 */
    Eof,
    /* 讀取：串流在一筆記錄的中途結束。 */
    Incomplete,
    /* 讀取：這筆記錄超出呼叫端給的位元組預算。 */
    TooLong,
    /* 讀取：串流回報錯誤。 */
    ReadError,
    /* 讀寫共用：argv 行沒有任何引數。 */
    EmptyArgv,
    /* 讀寫共用：argv 行的引數超過 kInstArgvMax。 */
    TooManyArgs,
    /* 寫入：某個值含有格式用作分隔符號的位元組。 */
    ArgumentContainsTab,
    ArgumentContainsLineBreak,
    FieldContainsLineBreak,
    /* 寫入：串流回報錯誤，可能已寫出半筆記錄。 */
    WriteError,
    /*
     * 讀寫共用：env 的某一筆不是合法的 KEY=VALUE —— 沒有 '='、鍵是空的，
     * 或含有格式用作分隔符號的位元組。這幾種情形合成一個狀態，因為 env 的
     * 產生端是機器而不是人，分得再細也沒有人會據此分開處理。
     */
    EnvEntryMalformed,
    /* 讀寫共用：env 行的筆數超過 kInstEnvMax。 */
    TooManyEnv,
    /*
     * 讀取：八行欄位之後那一行不是空的，代表記錄錯位。它的數值刻意跳到 16，
     * 對齊 C 介面裡 AOS_INST_MISSING_SEPARATOR —— 14 與 15 在 C 端已被只存在
     * 於 C 的 ALLOC_FAILED／BUFFER_TOO_SMALL 佔用，兩邊的列舉值必須逐一相等
     * (見 capi.cpp 的 static_assert)。
     */
    MissingSeparator = 16
};

/*
 * One instruction.
 *
 * Every field owns its own bytes, so an instruction stays valid after the
 * stream it came from is gone, and one built by hand for write_instruction
 * is no different from one that was read. argv.size() is the argument
 * count; there is no trailing null entry, and no field can be null.
 */
struct AOS_API inst_t {
    std::vector<std::string> argv;
    /* 第 2 到 6 行是路徑；空行就是空字串。 */
    std::string stdin_path;
    std::string stdout_path;
    std::string stderr_path;
    std::string exit_path;
    std::string cwd;
    /*
     * 第 7 行：環境變數清單，每個元素是一整串 "KEY=VALUE"。空的清單代表純
     * 繼承呼叫端的環境；非空則在繼承的環境上擴充 —— 覆寫同名、新增其餘、
     * 保留其他，同名以清單裡的後者為準。
     *
     * 存成扁平的清單而不是 map，是因為這一層對 env 的本分只有「照收、逐筆
     * 套用給子程式」：查詢與修改單一變數屬於產生端，它自己用 map，攤平成幾筆
     * KEY=VALUE 再交過來。這裡因此一行查詢碼都不需要，而清單既貼近格式，也
     * 貼近逐筆 setenv 時要的形狀。
     */
    std::vector<std::string> env;
    /* 第 8 行對解析器而言是不透明資料，可以是空的。 */
    std::string extra;

    /* 清空每個欄位，回到預設建構後的狀態。 */
    void clear();

    /* 沒有任何引數時為 true，也就是讀取失敗後的狀態。 */
    bool empty() const { return argv.empty(); }
};

/*
 * Read the next record from in.
 *
 * Returns InstState::Eof when the stream ended cleanly between records. On
 * that and on every other failure inst is left cleared, so a half-parsed
 * record can never be mistaken for a whole one and the previous record
 * never survives a failed read.
 *
 * max_record_bytes bounds the eight field lines' combined length, excluding
 * line terminators and the blank separator, and must be positive: without it
 * one malformed line would
 * grow a std::string without bound, and an instruction file is a trust
 * boundary. Exceeding it is InstState::TooLong. The budget is a runtime
 * argument rather than a compile-time constant so that the value a shared
 * library was built with is the value that governs.
 *
 * The bytes consumed so far are gone on failure. A stream cannot be
 * rewound, so InstState::Incomplete and InstState::TooLong end the run
 * rather than inviting a retry.
 */
AOS_API InstState read_instruction(std::istream &in, inst_t &inst,
                           std::size_t max_record_bytes = kInstRecordMaxBytes);

/*
 * Write one instruction to out as nine lines -- eight field lines plus a
 * trailing blank separator, each ending in LF -- so repeated calls append
 * records that read_instruction consumes.
 *
 * The whole record is validated before the first byte is written, so a
 * rejected instruction leaves the stream untouched. A write that fails
 * part-way through may still have emitted a partial record.
 */
AOS_API InstState write_instruction(std::ostream &out, const inst_t &inst);

/* The argv and env limits compiled into this library. */
AOS_API std::size_t inst_argv_max();
AOS_API std::size_t inst_env_max();

/* Return a static, human-readable description of state. */
AOS_API const char *to_string(InstState state);

/*
 * Build the null-terminated argument vector execv() and friends expect.
 *
 * The pointers borrow inst.argv's storage, so the result is valid only
 * while inst is alive and unmodified. inst is taken by non-const reference
 * because execv takes char *const *, not const char *const *; this is where
 * that wart is contained, rather than at every call site.
 */
AOS_API std::vector<char *> to_c_argv(inst_t &inst);

}  /* namespace aos */

#endif
