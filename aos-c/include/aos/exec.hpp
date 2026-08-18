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
 * Every state but Ok is a failure of the runtime itself. A child that ran
 * and exited non-zero is Ok: its status is data, reported in ExecResult and
 * written to exit_path, not an error here.
 */
enum class ExecState {
    Ok = 0,
    InvalidArgument,
    /* 重導向的檔案開不起來，個別回報以便指出是哪一個。 */
    OpenStdinFailed,
    OpenStdoutFailed,
    OpenStderrFailed,
    /* env_path 指的檔案讀不到，或其中某一行不是 KEY=VALUE。 */
    EnvFileFailed,
    /* cwd 切不過去。 */
    ChdirFailed,
    /* fork 失敗，或子行程根本沒能 exec 起來（例如找不到指令）。 */
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
     * 子行程失敗的 errno，只在 execute() 回傳失敗時有意義，且僅當該失敗來
     * 自作業系統呼叫時才非零。用來把「找不到指令」和「權限不足」分開。
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
 *               and the decimal status plus a newline is written to it.
 *   cwd         empty inherits the caller's working directory.
 *   env_path    empty inherits the caller's environment entirely.
 *               Otherwise the file is read as one KEY=VALUE per line and
 *               *replaces* the environment; it does not extend it. Blank
 *               lines are ignored, lines beginning with '#' are comments,
 *               and any other line without '=' is EnvFileFailed.
 *   extra       ignored.
 *
 * A failure to spawn -- command not found, cwd missing, a redirection
 * target that will not open -- is a runtime error reported here, and
 * nothing is written to exit_path. It is deliberately not recorded as if it
 * were an exit status: a child that genuinely exits 127 must stay
 * distinguishable from one that never started.
 */
AOS_API ExecState execute(inst_t &inst, ExecResult &result);

/* Return a static, human-readable description of state. */
AOS_API const char *to_string(ExecState state);

}  /* namespace aos */

#endif
