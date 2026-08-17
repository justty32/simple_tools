# aos-c

Reads a file of instructions and runs each one as a process. C++11, built
with a plain Makefile and no dependencies.

## The instruction format

One instruction is eight lines, one field per line:

```text
argv          tab-separated; no quoting or escaping
stdin_path    empty inherits the caller's handle
stdout_path   empty inherits; otherwise created and truncated
stderr_path   empty inherits; otherwise created and truncated
exit_path     empty discards the status; otherwise the exit code is written here
cwd           empty inherits the caller's working directory
env_path      empty inherits the environment; otherwise a KEY=VALUE file replaces it
extra         reserved, ignored
```

Every line, including the eighth, must end with a newline; that is what
tells a truncated record apart from a complete one whose last line is empty.
Records follow each other with no separator. LF and CRLF are both accepted.

So this runs `echo hello world` with its output captured and its exit status
recorded:

```text
echo→hello world
(empty)
out.txt
(empty)
status.txt
(empty)
(empty)
(empty)
```

where `→` is a tab. Note that the space in `hello world` is an ordinary
character: only tabs separate arguments.

> Running an instruction file executes arbitrary commands with your
> privileges. Write access to one is equivalent to code execution.

## Run

```sh
aos-c instructions.txt   # or pipe records in on stdin
```

Instructions run in order. A command that exits non-zero does not stop the
run — that status is data, and `exit_path` is where it goes. A command that
could not be started does stop it.

## Build

On POSIX platforms (Linux, macOS):

```sh
make debug
make release
```

On Windows with MinGW-w64:

```powershell
C:\dev\mingw64\bin\mingw32-make.exe debug
```

The executables are written to `build/debug/` and `build/release/`. Note
that process spawning is implemented for POSIX only; on other platforms
`aos::execute` returns `ExecState::PlatformUnsupported`.

Run the test suite, or build and test with `-Werror`:

```sh
make test
make strict
```

Remove all build output with `make clean`.

## Layout

- `include/aos/inst.hpp` — the instruction type, reader and writer. Portable
  C++11, knows nothing about processes.
- `include/aos/exec.hpp` — running one instruction. The only
  platform-specific part of the project.
- `src/` — implementations and the program entry point.
- `tests/` — standalone test sources using the `CHECK` macro from
  `tests/test_check.hpp`, no framework, built and run via `make test`.
- `docs/architecture.md` — why the layers are split where they are, every
  field semantic that the format itself leaves undefined, and what the
  design deliberately gives up.
