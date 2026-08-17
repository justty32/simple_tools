#ifndef AOS_INST_HPP
#define AOS_INST_HPP

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

/*
 * One instruction: a serialized process spawn, and the reader and writer
 * that move it between a stream and memory.
 *
 * An instruction is eight lines, one per field:
 *   argv, stdin, stdout, stderr, exit, cwd, env, extra.
 * The argv line is tab-separated with no quoting or escaping; spaces,
 * quotes and backslashes are ordinary characters, and adjacent tabs
 * preserve empty arguments. Every line, including the eighth, must end with
 * a newline, which is what lets a truncated record be told apart from a
 * complete one whose eighth line happens to be empty. Both LF and CRLF are
 * accepted on input; the writer always emits LF.
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

/* 一筆序列化記錄的行數；更動它就是更動格式。 */
constexpr std::size_t kInstLineCount = 8;

/*
 * 單筆記錄的預設位元組預算，計算八行的內容長度，不含行尾字元。這只是預設
 * 值，不是 API 的上限：任何正值都可以傳給 read_instruction。
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
    WriteError
};

/*
 * One instruction.
 *
 * Every field owns its own bytes, so an instruction stays valid after the
 * stream it came from is gone, and one built by hand for write_instruction
 * is no different from one that was read. argv.size() is the argument
 * count; there is no trailing null entry, and no field can be null.
 */
struct Instruction {
    std::vector<std::string> argv;
    /* 第 2 到 6 行是路徑；空行就是空字串。 */
    std::string stdin_path;
    std::string stdout_path;
    std::string stderr_path;
    std::string exit_path;
    std::string cwd;
    /* 第 7、8 行對解析器而言是不透明資料，可以是空的。 */
    std::string env_path;
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
 * max_record_bytes bounds the eight lines' combined length, excluding line
 * terminators, and must be positive: without it one malformed line would
 * grow a std::string without bound, and an instruction file is a trust
 * boundary. Exceeding it is InstState::TooLong. The budget is a runtime
 * argument rather than a compile-time constant so that the value a shared
 * library was built with is the value that governs.
 *
 * The bytes consumed so far are gone on failure. A stream cannot be
 * rewound, so InstState::Incomplete and InstState::TooLong end the run
 * rather than inviting a retry.
 */
InstState read_instruction(std::istream &in, Instruction &inst,
                           std::size_t max_record_bytes = kInstRecordMaxBytes);

/*
 * Write one instruction to out as eight lines, each ending in LF, so
 * repeated calls append records that read_instruction consumes.
 *
 * The whole record is validated before the first byte is written, so a
 * rejected instruction leaves the stream untouched. A write that fails
 * part-way through may still have emitted a partial record.
 */
InstState write_instruction(std::ostream &out, const Instruction &inst);

/* The argv limit compiled into this library. */
std::size_t inst_argv_max();

/* Return a static, human-readable description of state. */
const char *to_string(InstState state);

/*
 * Build the null-terminated argument vector execv() and friends expect.
 *
 * The pointers borrow inst.argv's storage, so the result is valid only
 * while inst is alive and unmodified. inst is taken by non-const reference
 * because execv takes char *const *, not const char *const *; this is where
 * that wart is contained, rather than at every call site.
 */
std::vector<char *> to_c_argv(Instruction &inst);

}  /* namespace aos */

#endif
