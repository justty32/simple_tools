#include "aos/aos.h"

#include "aos/exec.hpp"
#include "aos/inst.hpp"

#include <cstdio>
#include <cstring>
#include <istream>
#include <new>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>

/*
 * The C boundary. Three things happen here and nowhere else:
 *
 *   1. No exception escapes. Every entry point that can allocate is wrapped,
 *      because unwinding past an extern "C" frame into a C caller is
 *      undefined behaviour.
 *   2. The C enum values are checked against the C++ ones at compile time,
 *      so the two cannot drift apart silently.
 *   3. FILE * is bridged to std::istream / std::ostream, since neither can
 *      cross a C boundary.
 *
 * Nothing else in the project knows this file exists.
 */

/* 不透明控制代碼的實體。C 端只看得到前向宣告，所以佈局不屬於 ABI。 */
struct aos_instruction {
    aos::inst_t inst;
};

namespace {

/*
 * 兩組列舉必須逐值對應，否則跨越邊界時狀態會被默默改寫。與其在轉換時比
 * 對，不如讓不一致直接編不過。
 */
#define AOS_CHECK_STATE(cxx, c) \
    static_assert(static_cast<int>(cxx) == (c), "enum drift: " #cxx " != " #c)

AOS_CHECK_STATE(aos::InstState::Ok, AOS_INST_OK);
AOS_CHECK_STATE(aos::InstState::InvalidArgument, AOS_INST_INVALID_ARGUMENT);
AOS_CHECK_STATE(aos::InstState::Eof, AOS_INST_EOF);
AOS_CHECK_STATE(aos::InstState::Incomplete, AOS_INST_INCOMPLETE);
AOS_CHECK_STATE(aos::InstState::TooLong, AOS_INST_TOO_LONG);
AOS_CHECK_STATE(aos::InstState::ReadError, AOS_INST_READ_ERROR);
AOS_CHECK_STATE(aos::InstState::EmptyArgv, AOS_INST_EMPTY_ARGV);
AOS_CHECK_STATE(aos::InstState::TooManyArgs, AOS_INST_TOO_MANY_ARGS);
AOS_CHECK_STATE(aos::InstState::ArgumentContainsTab,
                AOS_INST_ARGUMENT_CONTAINS_TAB);
AOS_CHECK_STATE(aos::InstState::ArgumentContainsLineBreak,
                AOS_INST_ARGUMENT_CONTAINS_LINE_BREAK);
AOS_CHECK_STATE(aos::InstState::FieldContainsLineBreak,
                AOS_INST_FIELD_CONTAINS_LINE_BREAK);
AOS_CHECK_STATE(aos::InstState::WriteError, AOS_INST_WRITE_ERROR);
AOS_CHECK_STATE(aos::InstState::EnvEntryMalformed,
                AOS_INST_ENV_ENTRY_MALFORMED);
AOS_CHECK_STATE(aos::InstState::TooManyEnv, AOS_INST_TOO_MANY_ENV);

AOS_CHECK_STATE(aos::ExecState::Ok, AOS_EXEC_OK);
AOS_CHECK_STATE(aos::ExecState::InvalidArgument, AOS_EXEC_INVALID_ARGUMENT);
AOS_CHECK_STATE(aos::ExecState::SpawnFailed, AOS_EXEC_SPAWN_FAILED);
AOS_CHECK_STATE(aos::ExecState::WaitFailed, AOS_EXEC_WAIT_FAILED);
AOS_CHECK_STATE(aos::ExecState::ExitWriteFailed, AOS_EXEC_EXIT_WRITE_FAILED);

#undef AOS_CHECK_STATE

/*
 * 把 FILE * 接成 streambuf。實作端要的是 std::istream/std::ostream，而
 * FILE * 才是 C 呼叫端手上有的東西。這裡不另外緩衝：FILE 自己已經緩衝過，
 * 再加一層只會讓兩邊的檔案位置對不起來。
 */
class FileBuf : public std::streambuf {
public:
    explicit FileBuf(std::FILE *file) : file_(file), ch_(0) {}

protected:
    int_type underflow() override
    {
        const int c = std::fgetc(file_);

        if (c == EOF) {
            return traits_type::eof();
        }
        ch_ = static_cast<char>(c);
        setg(&ch_, &ch_, &ch_ + 1);
        return traits_type::to_int_type(ch_);
    }

    int_type overflow(int_type c) override
    {
        if (traits_type::eq_int_type(c, traits_type::eof())) {
            return traits_type::not_eof(c);
        }
        if (std::fputc(traits_type::to_char_type(c), file_) == EOF) {
            return traits_type::eof();
        }
        return c;
    }

    std::streamsize xsputn(const char *s, std::streamsize n) override
    {
        return static_cast<std::streamsize>(
            std::fwrite(s, 1, static_cast<std::size_t>(n), file_));
    }

private:
    std::FILE *file_;
    char ch_;
};

/* 單一字串欄位的查表；未知欄位回傳 nullptr。 */
const std::string *field_of(const aos::inst_t &inst, aos_inst_field field)
{
    switch (field) {
    case AOS_FIELD_STDIN:
        return &inst.stdin_path;
    case AOS_FIELD_STDOUT:
        return &inst.stdout_path;
    case AOS_FIELD_STDERR:
        return &inst.stderr_path;
    case AOS_FIELD_EXIT:
        return &inst.exit_path;
    case AOS_FIELD_CWD:
        return &inst.cwd;
    case AOS_FIELD_EXTRA:
        return &inst.extra;
    }
    return nullptr;
}

/*
 * 設定用的版本。const_cast 只出現在這裡，而這裡的來源本來就是非 const 的
 * 指令，所以它不可能把唯讀路徑變成寫入 —— 查表本身維持 const。
 */
std::string *mutable_field_of(aos::inst_t &inst, aos_inst_field field)
{
    return const_cast<std::string *>(
        field_of(static_cast<const aos::inst_t &>(inst), field));
}

}  /* namespace */

extern "C" {

aos_instruction *aos_instruction_new(void)
{
    return new (std::nothrow) aos_instruction();
}

void aos_instruction_free(aos_instruction *inst)
{
    delete inst;
}

void aos_instruction_clear(aos_instruction *inst)
{
    if (inst != nullptr) {
        inst->inst.clear();
    }
}

size_t aos_instruction_argc(const aos_instruction *inst)
{
    return (inst == nullptr) ? 0 : inst->inst.argv.size();
}

const char *aos_instruction_arg(const aos_instruction *inst, size_t index)
{
    if (inst == nullptr || index >= inst->inst.argv.size()) {
        return nullptr;
    }
    return inst->inst.argv[index].c_str();
}

size_t aos_instruction_env_count(const aos_instruction *inst)
{
    return (inst == nullptr) ? 0 : inst->inst.env.size();
}

const char *aos_instruction_env(const aos_instruction *inst, size_t index)
{
    if (inst == nullptr || index >= inst->inst.env.size()) {
        return nullptr;
    }
    return inst->inst.env[index].c_str();
}

const char *aos_instruction_field(const aos_instruction *inst,
                                  aos_inst_field field)
{
    if (inst == nullptr) {
        return nullptr;
    }

    const std::string *value = field_of(inst->inst, field);

    return (value == nullptr) ? nullptr : value->c_str();
}

aos_inst_state aos_instruction_push_arg(aos_instruction *inst,
                                        const char *value)
{
    if (inst == nullptr || value == nullptr) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    try {
        inst->inst.argv.push_back(value);
    } catch (...) {
        /* 實作端唯一會拋的就是配置失敗（bad_alloc 或 length_error）。 */
        return AOS_INST_ALLOC_FAILED;
    }
    return AOS_INST_OK;
}

aos_inst_state aos_instruction_push_env(aos_instruction *inst,
                                        const char *entry)
{
    if (inst == nullptr || entry == nullptr) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    try {
        inst->inst.env.push_back(entry);
    } catch (...) {
        return AOS_INST_ALLOC_FAILED;
    }
    return AOS_INST_OK;
}

aos_inst_state aos_instruction_set_field(aos_instruction *inst,
                                         aos_inst_field field,
                                         const char *value)
{
    if (inst == nullptr || value == nullptr) {
        return AOS_INST_INVALID_ARGUMENT;
    }

    std::string *target = mutable_field_of(inst->inst, field);

    if (target == nullptr) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    try {
        target->assign(value);
    } catch (...) {
        return AOS_INST_ALLOC_FAILED;
    }
    return AOS_INST_OK;
}

aos_inst_state aos_instruction_read(FILE *stream, aos_instruction *inst,
                                    size_t max_record_bytes)
{
    if (stream == nullptr || inst == nullptr) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    try {
        FileBuf buf(stream);
        std::istream in(&buf);
        const aos::InstState state =
            aos::read_instruction(in, inst->inst, max_record_bytes);

        /*
         * streambuf 沒有辦法把 FILE 的錯誤旗標變成 badbit，讀取端只會看到
         * 沒有更多位元組。真正的 I/O 錯誤因此在這裡補判，否則會被誤報成
         * 輸入正常結束。
         */
        if (state != aos::InstState::Ok && std::ferror(stream)) {
            return AOS_INST_READ_ERROR;
        }
        return static_cast<aos_inst_state>(static_cast<int>(state));
    } catch (...) {
        return AOS_INST_ALLOC_FAILED;
    }
}

aos_inst_state aos_instruction_write(FILE *stream, const aos_instruction *inst)
{
    if (stream == nullptr || inst == nullptr) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    try {
        FileBuf buf(stream);
        std::ostream out(&buf);
        const aos::InstState state = aos::write_instruction(out, inst->inst);

        if (state == aos::InstState::Ok && std::ferror(stream)) {
            return AOS_INST_WRITE_ERROR;
        }
        return static_cast<aos_inst_state>(static_cast<int>(state));
    } catch (...) {
        return AOS_INST_ALLOC_FAILED;
    }
}

aos_inst_state aos_instruction_write_buffer(const aos_instruction *inst,
                                            char *buffer, size_t size,
                                            size_t *needed)
{
    if (needed != nullptr) {
        *needed = 0;
    }
    if (inst == nullptr) {
        return AOS_INST_INVALID_ARGUMENT;
    }
    try {
        std::ostringstream out;
        const aos::InstState state = aos::write_instruction(out, inst->inst);

        if (state != aos::InstState::Ok) {
            return static_cast<aos_inst_state>(static_cast<int>(state));
        }

        const std::string bytes = out.str();

        if (needed != nullptr) {
            *needed = bytes.size();
        }
        /* 必須容得下結尾的 NUL，所以 size 要嚴格大於內容長度。 */
        if (buffer == nullptr || size <= bytes.size()) {
            return AOS_INST_BUFFER_TOO_SMALL;
        }
        std::memcpy(buffer, bytes.data(), bytes.size());
        buffer[bytes.size()] = '\0';
        return AOS_INST_OK;
    } catch (...) {
        return AOS_INST_ALLOC_FAILED;
    }
}

aos_exec_state aos_instruction_execute(aos_instruction *inst,
                                       aos_exec_result *result)
{
    if (result != nullptr) {
        result->status = 0;
        result->signalled = 0;
        result->error = 0;
    }
    if (inst == nullptr) {
        return AOS_EXEC_INVALID_ARGUMENT;
    }
    try {
        aos::ExecResult native;
        const aos::ExecState state = aos::execute(inst->inst, native);

        if (result != nullptr) {
            result->status = native.status;
            result->signalled = native.signalled ? 1 : 0;
            result->error = native.error;
        }
        return static_cast<aos_exec_state>(static_cast<int>(state));
    } catch (...) {
        return AOS_EXEC_ALLOC_FAILED;
    }
}

size_t aos_inst_argv_max(void)
{
    return aos::inst_argv_max();
}

size_t aos_inst_env_max(void)
{
    return aos::inst_env_max();
}

size_t aos_inst_record_max_bytes(void)
{
    return aos::kInstRecordMaxBytes;
}

const char *aos_inst_state_string(aos_inst_state state)
{
    const int value = static_cast<int>(state);

    /* 這兩個狀態只存在於 C 介面，C++ 端沒有對應可以轉。 */
    if (value == AOS_INST_ALLOC_FAILED) {
        return "out of memory";
    }
    if (value == AOS_INST_BUFFER_TOO_SMALL) {
        return "output buffer is too small";
    }
    if (value < AOS_INST_OK || value > AOS_INST_TOO_MANY_ENV) {
        return "unknown instruction result";
    }
    return aos::to_string(static_cast<aos::InstState>(value));
}

const char *aos_exec_state_string(aos_exec_state state)
{
    const int value = static_cast<int>(state);

    if (value == AOS_EXEC_ALLOC_FAILED) {
        return "out of memory";
    }
    if (value < AOS_EXEC_OK || value > AOS_EXEC_EXIT_WRITE_FAILED) {
        return "unknown execution result";
    }
    return aos::to_string(static_cast<aos::ExecState>(value));
}

const char *aos_version_string(void)
{
    return "0.1.0";
}

}  /* extern "C" */
