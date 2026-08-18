#ifndef AOS_EXEC_HPP
#define AOS_EXEC_HPP

#include "inst.hpp"

/*
 * Execution: turning one inst_t into a spawned process.
 *
 * This layer takes an inst_t and nothing else -- never a path, never a
 * stream -- so it stays testable without touching the reader, and it does
 * not care where the record came from. Looping over many instructions
 * belongs to the caller.
 *
 * This is the first non-portable component in the project. Everything below
 * inst.hpp is plain C++11; process spawning is not. The platform split is
 * contained entirely in exec.cpp, behind the one function declared here.
 * POSIX is implemented; on any other platform execute() compiles and
 * returns ExecState::PlatformUnsupported rather than pretending.
 */
namespace aos {

/*
 * Result of trying to run one instruction.
 *
 * Every state but Ok is a failure of this library itself, and there are
 * few of them left on purpose. A child that ran and exited non-zero is Ok,
 * and so is a child that never got as far as its own main(): a redirection
 * that would not open, a cwd that does not exist, a command that is not on
 * PATH -- all of those are now an exit status like any other, 126 or 127,
 * reported in ExecResult and written to exit_path. This is what a shell
 * does, and the reason to match it is that exit_path's readers need "this
 * one is finished" to be one signal, not two.
 */
enum class ExecState {
    Ok = 0,
    InvalidArgument,
    /*
     * fork 失敗。這是唯一還在這裡回報的啟動錯誤，因為它是唯一一種連子行程
     * 都不存在、沒有結束碼可以承載的失敗。
     */
    SpawnFailed,
    /* 子行程起來了，但等待它結束時出錯。 */
    WaitFailed,
    /* 子行程結束了，但結束狀態寫不進 exit_path。 */
    ExitWriteFailed,
    PlatformUnsupported
};

/* How the child ended. Only meaningful when execute() returned Ok. */
struct ExecResult {
    /*
     * 正常結束時是行程的結束碼；被訊號終止時是 128 + 訊號編號，沿用 shell
     * 的慣例，讓這個欄位在兩個平台上都只是一個數字。
     */
    int status = 0;
    /* 為 true 代表 status 來自訊號而非 exit()。 */
    bool signalled = false;
    /*
     * 失敗的 errno，只在 execute() 回傳 SpawnFailed 或 WaitFailed 時有意義。
     * 子行程自己的失敗不再走這裡，它走 status（126／127）。
     */
    int error = 0;
};

/*
 * Run one instruction to completion and report how its process ended.
 *
 * The instruction is taken by non-const reference only because execv wants
 * char *const *; nothing about it is modified.
 *
 * Field semantics, none of which the format itself defines. These are
 * choices, and changing one is a compatibility break, not a bug fix:
 *
 *   argv        argv[0] is looked up on PATH, as execvp does.
 *   stdin_path  empty inherits the caller's stdin; otherwise opened for
 *   stdout_path reading, or for writing with O_CREAT and O_TRUNC. Output
 *   stderr_path files are truncated, matching a shell's `>` rather than
 *               `>>`, because appending cannot be undone by the caller and
 *               truncating can be avoided by choosing a fresh path.
 *   exit_path   empty discards the status; otherwise the file is truncated
 *               and the decimal status plus a newline is written to it,
 *               whether the command succeeded, failed, or never started.
 *               It marks the instruction as finished, not as correct.
 *   cwd         empty inherits the caller's working directory.
 *   env         empty inherits the caller's environment entirely.
 *               Otherwise the entries *replace* the environment; they do
 *               not extend it. The list is passed to the child as it
 *               stands: nothing here reads, merges, sorts or de-duplicates
 *               it, because deciding what the environment should contain
 *               belongs to whoever produced the instruction.
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
 * those codes, exactly as in a shell. Telling the two apart would need a
 * second channel out of the child, and the caller who cares about what the
 * command did is reading its stdout and stderr anyway.
 */
AOS_API ExecState execute(inst_t &inst, ExecResult &result);

/* Return a static, human-readable description of state. */
AOS_API const char *to_string(ExecState state);

}  /* namespace aos */

#endif
