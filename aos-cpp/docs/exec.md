# Execution semantics

Records run sequentially in input order. Each field maps directly to child
process setup:

- `argv` becomes the null-terminated argument vector passed to `execve`.
- Non-empty `stdin` is opened read-only and duplicated onto fd 0.
- Non-empty `stdout` and `stderr` are created with mode `0666` (subject to the
  caller's umask), truncated, and duplicated onto fd 1 or 2.
- Non-empty `cwd` is applied with `chdir` after redirection and before `execve`.
- `env` is merged over the inherited environment: named entries replace or add,
  while every unmentioned entry remains. The resulting `envp` goes to `execve`.
- Nonzero `timeout_ms` enables the deadline described below.
- Non-empty `exit` is created/truncated by the parent after waiting and receives
  the reported decimal status followed by LF.

Empty path fields inherit the corresponding process setting. There is no shell
interpretation: arguments are not split, expanded, or treated as redirection
syntax. Use `argv: ["sh", "-c", "..."]` only when shell behavior is intended.
Redirection files are opened before the child applies `cwd`, and the parent
writes `exit` without changing directory. Relative paths in those four fields
therefore start from the caller's directory, not from `cwd`.

## PATH lookup

Lookup is prepared in the parent; the implementation does not call `execvp`.
The merged environment is used, so a record's `env.PATH` affects its command.
When `PATH` is unset, `_CS_PATH` supplies the system default; if that query
fails, `/bin:/usr/bin` is used. An empty PATH component means the child's
effective current directory. Relative PATH components are also resolved from
that directory, including a requested `cwd`.

Candidates are tried in PATH order. A candidate must be a regular executable
file. Permission-denied or otherwise present-but-not-executable candidates are
remembered, but lookup continues in later components. If nothing executable is
found, a remembered access failure produces status 126; no candidate produces
127. If `argv[0]` contains `/`, it is passed through without searching. A
relative value is consequently resolved by `execve` after the child changes to
`cwd`.

## Status and failures

A normally exiting child reports its own status unchanged. A child terminated
by signal `N` reports `128 + N` and sets `signalled`. Setup failure—including
process-group setup, redirection, or `chdir`—exits 126. Failure to locate the
command, or a final `execve` failure, exits 127. Because 126 and 127 are ordinary
8-bit exit values, they cannot be distinguished from a program that genuinely
exits with the same value.

A nonzero child status, signal termination, PATH miss, setup status, or timeout
is a completed execution, not an `ExecState` failure. It does not stop later
records, and the CLI remains successful if every record reaches a status and
every requested status file is written.

Library failures are different: invalid empty `argv`, failure to fork or set up
the parent side of the process group, failure while waiting, and failure to
write `exit` return a non-`Ok` `ExecState`. The CLI reports these, continues with
later records, and finally returns 1. `ExecResult.error` carries `errno` where
the failing operation supplies one.

## Timeouts and process groups

With `timeout_ms == 0`, the parent blocks in `waitpid`. A positive value uses a
monotonic clock and polling that grows from 1 ms to at most 50 ms. At the
deadline the parent marks `timed_out`, sends `SIGTERM` to the entire child
process group, and grants 2000 ms for orderly shutdown. If the direct child is
still alive it sends `SIGKILL` to that group and waits synchronously.

The group target matters: descendants that remain in the command's group
receive the same signal as the direct child. Once the direct child has been
reaped the parent sends `SIGKILL` to the group in either case, because the
direct child dying does not mean the group is empty -- a grandchild that ignores
`SIGTERM` would otherwise outlive the deadline, and reaching descendants is the
whole reason the group is there. `SIGKILL` cannot be caught, and an already
empty group makes the call a harmless `ESRCH`. Nothing is signalled when the
command finishes on its own: leaving background children behind is the
instruction's business, not a timeout. The usual reported statuses are 143 for `SIGTERM` and 137 for
`SIGKILL`; `timed_out` distinguishes a library-initiated deadline from the same
signal sent elsewhere.

## Security

An instruction file is executable authority. It can name arbitrary programs,
arguments, input files, output files, working directories, and environment
values, all using the caller's credentials. Running an untrusted instruction
file is equivalent to running arbitrary commands with your permissions. Anyone
who can modify a file that `aos-cpp` will consume effectively has arbitrary code
execution as that caller; protect its write permissions and its generation
path accordingly.
