# Execution semantics

Records run sequentially in input order. When `argv[0]` has no slash, the parent
searches for it using the command's merged `PATH` before `fork`; an unset `PATH`
uses the system default, and empty fields mean the command's current directory.
An `argv[0]` containing a slash is passed unchanged so a relative path is
resolved after the child changes to `cwd`.
Empty path fields inherit the corresponding process setting. Non-empty standard
stream paths are opened in the child; output files are created and truncated.
`cwd` changes the child directory, and `env` extends the inherited environment.
The parent materializes that merged environment as `envp`, and the child passes
it directly to `execve`; no environment allocation or PATH search happens after
`fork`.

If child setup fails, its status is 126. A PATH search that finds a candidate but
cannot execute it also produces 126 after all later entries have been tried; a
search with no candidate produces 127. Other `execve` failures produce 127.
Normal exits retain their status; signal termination uses `128 + signal`. A
nonzero child status is data, not a runner failure, and does not stop later
records. When `exit` is set, that status is written as decimal text followed by
LF.

Only runner failures—forking, waiting, or writing an `exit` file—make the CLI
return 1. Later records still run. If every record reaches a child status and
any requested status file is written, the CLI returns 0.

## Timeouts

A positive `timeout_ms` uses a monotonic clock. At the deadline, `aos-cpp` sends
SIGTERM to the command's process group. It allows 2000 ms for shutdown, then
sends SIGKILL and waits for the child. The resulting status is normally 143 or
137, and timeout remains a completed execution rather than a runner failure.
Zero disables the deadline and waits without polling.
