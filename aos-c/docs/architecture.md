# Architecture

## Layers

```text
┌─ main ──────────────┐  ┌─ C shim ─────────────────────────┐
│  open a stream      │  │  opaque handle, plain enums,     │
│  -> read records    │  │  FILE *, no exceptions escape    │
│  -> execute them    │  │                     aos/aos.h    │
├─ execution ─────────┴──┴──────────────────────────────────┤
│  inst_t  ->  a spawned process           aos/exec.hpp │
├────────────────────────────────────────────────────────────┤
│  inst_t                                  aos/inst.hpp │
│  read:   std::istream  ->  inst_t   (one per call)    │
│  write:  inst_t   ->  std::ostream                    │
└────────────────────────────────────────────────────────────┘
```

The C shim in `capi.cpp` sits beside `main` rather than under it: it is a
second way in, not a layer the rest of the code goes through. Nothing below
it knows it exists.

`aos/inst.hpp` is the only parser in the project. It moves one record
between a stream and an `inst_t`, in both directions, and knows nothing
about processes. Its implementation is two files: `inst_format.cpp` is the
only code that knows a record is eight lines and that tabs separate `argv`
and `env`, while `inst.cpp` owns the type, its limits and its adapters and
never looks at a byte of the encoding. The line is drawn where the format
changes, so a format change touches one file. `aos/exec.hpp` takes an `inst_t` and knows nothing
about streams. Looping over many records belongs to the caller, which is all
`aos::run` in `run.cpp` does.

There is no file layer. A caller that wants every record in a file opens the
file and calls `read_instruction` until it returns `InstState::Eof`.

The project is C++11. Nothing below the execution layer uses anything newer,
and nothing below it is platform-specific.

This document is about *why* the code is shaped the way it is. The format
itself and the runtime behaviour of each field are specified separately, in
Chinese, for people who want to use the tool rather than change it:
[format.md](format.md) and [exec.md](exec.md).

## Why the core is shaped this way

The reader consumes exactly one record per call, straight from a
`std::istream`. Three things follow:

- **Pipes and `std::cin` work.** Nothing seeks, and nothing needs to know
  the length of the input in advance.
- **Memory is O(longest record), not O(input).** A hundred-gigabyte
  instruction stream is read through one `inst_t`.
- **The first record is available before the rest of the input exists.** A
  producer and a consumer can run at the same time.

### Every field owns its own bytes

`inst_t` is a plain struct of a `std::vector<std::string>` and seven
`std::string`s. That is the whole type. There is no shared buffer, no
offsets, no capacity bookkeeping, and no private section.

This is the part that changed most from the C version, where the same design
needed a `storage` buffer, an `argv_slots` array, two capacities, and eight
stack offsets to resolve after `realloc` moved things. All of it existed to
answer *who owns these bytes*, and `std::string` answers that by
construction. Three consequences:

- An `inst_t` stays valid after the stream it came from is gone.
- One built by hand for `write_instruction` is not a special case with its
  own ownership rules — it is the same type, and there is nothing to free.
- Three error states disappeared outright. `NullArgument` and `NullField`
  cannot happen because a `std::string` cannot be null, and allocation
  failure is `std::bad_alloc` rather than a state every caller must check.

`inst_t::clear()` clears each string rather than assigning a fresh one,
so the buffers survive into the next read. A caller reading a whole stream
through one instruction settles into steady state without reallocating:

```cpp
aos::inst_t inst;

while (aos::read_instruction(in, inst) == aos::InstState::Ok) {
    /* 使用 inst；它擁有自己的位元組，在下一次讀取前都有效 */
}
```

### Why the reader does not use std::getline

`std::getline` will read a line of any length into memory. That makes the
record budget below unenforceable, so the reader reads bytes and checks the
budget as it goes. That is the only reason; nothing else about the reader
wants byte-at-a-time input.

## What this design trades away

Recorded here so it is a decision and not an oversight.

- **Random access across records.** There is no array of every instruction
  in a file, and no "instruction 47". A caller that needs to revisit records
  keeps copies — which, unlike in the C version, is just a copy of the
  `inst_t`.
- **Retry after a short read.** A stream cannot be rewound, so
  `InstState::Incomplete` is terminal: the bytes are gone. A blocking stream
  already waits for a slow writer rather than reporting a short record.
- **Serializing into a caller's buffer.** The writer targets a
  `std::ostream`, which `std::ostringstream` makes a non-issue for anyone
  who wants bytes in memory.
- **A per-record error index.** The caller counts records itself.

## Failure leaves nothing behind

Every field is cleared when a read begins and set only once the record is
complete, so on **every** failing return — including `InstState::Eof` — the
instruction is empty. A caller cannot mistake a half-parsed record for a
whole one, and the previous record never survives a failed read.

`split_argv` counts and checks the arguments before it builds any of them,
so a rejected argv line leaves nothing half-built.

The writer is symmetric: it validates the entire record before emitting a
byte, so a rejected instruction leaves the stream untouched. The one
asymmetry left is physical — an I/O error part-way through a write may leave
a partial record on the stream, and no validation can prevent that.

## The record budget

The reader grows a `std::string` to fit whatever it is reading, so without a
limit one malformed line would allocate without bound. An instruction file
is a trust boundary, so a budget always applies:

```cpp
InstState read_instruction(std::istream &in, inst_t &inst,
                           std::size_t max_record_bytes = kInstRecordMaxBytes);
```

It counts the bytes of the eight lines as they arrive, excluding the LF that
ends each of them. A CRLF's CR counts, because the CR is stripped only after
the budget check. Exceeding the budget is `InstState::TooLong`. Zero is
rejected as an invalid argument rather than read as "unlimited".

The budget is a **default argument, not a macro**. A compile-time limit in a
public header is a trap for a shared library, where the value the library
was built with governs and a consumer that redefines the macro changes only
its own copy of the header. It is also why the test build needs no `-D`
override and therefore no separate object tree. `inst_argv_max()` exists for
the same reason: it reports the argument limit compiled into the library
rather than making consumers trust their copy of `kInstArgvMax`.

## Execution

An instruction is a serialized process spawn. `aos::execute` takes an
`inst_t` and nothing else — never a path, never a stream — so it stays
testable without touching the reader and does not care where the record came
from.

### The field semantics, now decided

The parser treats these fields as opaque bytes; execution cannot. Each of
these was undefined by the format, and each is now a choice. Changing one is
a compatibility break, not a bug fix.

| field | decision | why |
| --- | --- | --- |
| `argv` | `argv[0]` is looked up on `PATH`, as `execvp` does | matches what a user typing the same command would get |
| empty `stdin_path` / `stdout_path` / `stderr_path` | inherit the caller's handle | attaching the null device would silently swallow output |
| existing `stdout_path` / `stderr_path` | truncate (`O_TRUNC`) | matches a shell's `>`; appending cannot be undone by the caller, truncating can be avoided by choosing a fresh path |
| empty `cwd` | inherit the caller's working directory | |
| empty `env` | inherit the caller's environment entirely | |
| non-empty `env` | the entries **replace** the environment | extending is the caller's job to express; replacing is the reproducible option, and it is the one that cannot be built out of the other |
| an `env` entry that is not `KEY=VALUE` | `EnvEntryMalformed`, at parse time | better to fail than to guess, and better to fail before a process exists than after |
| duplicate keys in `env` | passed through verbatim | merging would mean owning a policy for which duplicate wins; the producer already has a map and can decide there |
| empty `exit_path` | the status is discarded | |
| non-empty `exit_path` | truncated, then the decimal status and a newline, **whatever happened** | it is the "this one is finished" signal, and a signal that is sometimes absent cannot be waited on |
| status of a signalled child | `128 + signum` | shell convention, and it keeps the field one number on every platform |
| a child that could not be set up (redirection, `chdir`) | status `126` | shell convention |
| `execvp` failing (no such command) | status `127` | shell convention; see below |
| `extra` | ignored | |
| `fork` failing | a runtime error, and **nothing** is written to `exit_path` | it is the one failure with no child, so there is no status to carry it |
| several instructions | sequential, and the run continues past every failure it can | later instructions do not depend on earlier ones, so abandoning them turns one failure into many things simply not done |
| a parse failure mid-file | fatal: the rest of the stream is not read | the format has no record separator, so a "still aligned" cursor is only aligned relative to what was already consumed; resuming would decode later records into garbage, which is worse than stopping |

### Not telling "command not found" from "the command exited 127"

Earlier versions did tell them apart, with the standard trick: the child
inherits the write end of a pipe marked `FD_CLOEXEC` and writes which step
failed and its `errno` if it cannot get as far as `execvp`; a successful
`execvp` closes the pipe and the parent reads zero bytes. It worked, and it
is gone.

It went because the distinction it bought conflicts with how the result is
consumed. A caller waits for `exit_path` to appear; a "never started" answer
has no exit code, so it cannot be written there, so that instruction's file
never appears and its reader waits forever. Fidelity to a shell is worth
more here: a shell reports 127 for a missing command and 126 for one it
could not execute, and nobody finds it lacking. What the command actually
did is a question its stdout and stderr answer.

So the child `_exit`s 126 or 127, the parent treats that as an ordinary
status, and `ExecState` keeps only the failures that cannot become one:
`fork` failing, `waitpid` failing, and `exit_path` not being writable. The
cost, stated plainly: a child that genuinely exits 126 or 127 is
indistinguishable from one that never ran — exactly as in a shell.

### The instruction stream's own file descriptor

`run.cpp` opens `argv[1]` with `open()` and wraps the descriptor in a
`streambuf`, rather than using `std::ifstream`, because two flags matter and
`std::ifstream` can portably set neither.

`O_CLOEXEC` is the important one. `fork` copies every descriptor to the
child, and a descriptor survives `exec` by default, so without it every
spawned command would inherit the instruction stream. On a regular file that
is merely untidy. On a FIFO it is a hang: the upstream writer keeps seeing a
reader, so it never gets EOF or `EPIPE`, possibly forever, long after aos-c
itself has exited.

`O_NONBLOCK` is only needed for the open itself: opening a FIFO for reading
blocks until a writer appears, which would turn "nothing to do" into "hang".
It is cleared immediately afterwards with `fcntl`, so reads go back to
blocking semantics -- which are the ones wanted here: no writer means an
immediate EOF and a quick exit, a writer with nothing ready means wait, and
the writer closing means EOF and a finished drain. On a regular file the
flag does nothing either way.

The descriptor for stdin needs none of this: it is already fd 0, and it is
the caller's, not ours to reopen. What it costs is documented rather than
fixed: a child with no `stdin_path` of its own inherits fd 0, which when the
instructions arrive on stdin is the instruction stream itself.

### Portability

This is the only non-portable component. POSIX (`fork`, `execvp`, `dup2`,
`chdir`, `waitpid`) is implemented. Windows needs `CreateProcess` with
`STARTUPINFO` handle redirection and has no `fork`, so it is a second
implementation rather than a variation on this one; until it exists,
`execute` compiles there and returns `ExecState::PlatformUnsupported` rather
than pretending. The split is contained entirely in `exec.cpp` — the header
is platform-neutral, and `inst.hpp` and the tests stay pure C++11.

`_POSIX_C_SOURCE` is defined at the top of `exec.cpp` because `-std=c++11`
defines `__STRICT_ANSI__`, which otherwise hides the POSIX declarations.

### The one wart the type system cannot remove

`execv` and friends take `char *const *`. `to_c_argv` builds that from an
`inst_t`, which is why it takes a non-const reference despite modifying
nothing. Containing the wart in one function is the most C++ can do about
it; the alternative is a `const_cast` at every call site.

### The instruction file is a trust boundary

Executing an instruction file runs arbitrary commands with the invoking
user's privileges. That is the intended purpose, not a flaw, but it means
write access to an instruction file is equivalent to code execution. Worth
stating plainly in the user-facing documentation.

## The two public boundaries

The project ships two interfaces on purpose, and the difference between them
is not what they can do -- it is what they promise.

| | `aos/aos.h` (C) | `aos/inst.hpp`, `aos/exec.hpp` (C++) |
| --- | --- | --- |
| consumer needs | a C compiler and a linker | the *same* compiler, standard library and ABI setting as the library |
| across versions | enum values and signatures are append-only | no promise |
| out of memory | `AOS_INST_ALLOC_FAILED` | `std::bad_alloc` |
| ownership | explicit `aos_instruction_free` | destructors |

The C++ interface cannot make the first promise. Putting `std::string` and
`std::vector` in a signature means a consumer built with a different
standard library, or a different `_GLIBCXX_USE_CXX11_ABI`, gets undefined
behaviour rather than a link error. That is not a flaw in the code; it is
what that kind of interface is. Shipping the C header alongside it is the
answer, and keeping both is cheaper than picking one, because the shim is
small and mechanical.

### What the shim actually has to do

Three things, all of them in `capi.cpp` and nowhere else.

**No exception may escape.** Unwinding past an `extern "C"` frame into a C
caller is undefined behaviour, so every entry point that can allocate is
wrapped and translates a throw into `AOS_INST_ALLOC_FAILED`. This is why the
C enum has one value the C++ enum does not: allocation failure has to become
a state when it can no longer be an exception.

**The two enums must not drift.** Every C value is checked against its C++
counterpart with `static_assert`, so adding a state to one and forgetting
the other is a compile error rather than a state that silently changes
meaning as it crosses.

**`FILE *` has to reach a `std::istream`.** A small `std::streambuf` bridges
them. It deliberately does no buffering of its own -- `FILE` already
buffers, and a second layer would leave the two disagreeing about the file
position. One consequence: a `streambuf` cannot turn a `FILE` error flag
into `badbit`, so the reader only sees the bytes stop. The shim checks
`ferror` afterwards and upgrades the result to `AOS_INST_READ_ERROR`, which
would otherwise be misreported as a clean end of input.

### Visibility

Everything is built with `-fvisibility=hidden`, so a symbol reaches the
library's surface only by carrying `AOS_API` from `aos/export.h`. Helpers
live in anonymous namespaces on top of that. The result is 16 C entry points
and 8 C++ ones, and nothing else -- `read_line`, `split_argv`, `FileBuf` and
`run_child` are all invisible from outside.

`make shared` builds `libaos.so.0.1.0` with a soname of `libaos.so.0`. The
soname only moves when the C ABI breaks; the C++ interface, having made no
promise, does not constrain it.

### Still outstanding

- **`inst_t`'s layout is ABI for the C++ interface.** The C one is
  immune -- `aos_instruction` is opaque, so fields can be added without
  touching a compiled consumer. That asymmetry is the clearest illustration
  of what the C boundary buys.
- **Error enums are append-only** on the C side. New states go at the end.
- **Windows.** Everything but `execute` works there, and the whole suite has
  been cross-compiled and run under MinGW -- 95 of the 126 cases, the
  remainder being the POSIX-only execution tests. Getting there found two
  real defects, both of which are the kind that only a second platform
  exposes: two helpers used solely by the POSIX branch tripped
  `-Wunused-function`, which `-Werror` turned into a build failure, and
  `AOS_API` needed a third state, because a statically linked consumer was
  being handed `__declspec(dllimport)` and so emitted `__imp_` references no
  DLL was there to satisfy. `execute` itself is still unimplemented.

## Suggested order

1. Windows `execute`, if both targets are still required. It is the only
   piece where the interface is settled and the implementation is missing.
2. An install target, once someone actually links against the library --
   headers, the soname symlinks, and a pkg-config file.
