# Architecture

## Layers

```text
┌─ main ──────────────────────────────────────────────────────┐
│  open a stream  ->  read records  ->  execute them          │
├─ execution ─────────────────────────────────────────────────┤
│  Instruction  ->  a spawned process        aos/exec.hpp     │
├─ core ──────────────────────────────────────────────────────┤
│  Instruction                               aos/inst.hpp     │
│  read:   std::istream  ->  Instruction   (one per call)     │
│  write:  Instruction   ->  std::ostream                     │
└─────────────────────────────────────────────────────────────┘
```

`aos/inst.hpp` is the only parser in the project. It moves one record
between a stream and an `Instruction`, in both directions, and knows nothing
about processes. `aos/exec.hpp` takes an `Instruction` and knows nothing
about streams. Looping over many records belongs to the caller, which is all
`aos::run` in `aos.cpp` does.

There is no file layer. A caller that wants every record in a file opens the
file and calls `read_instruction` until it returns `InstState::Eof`.

The project is C++11. Nothing below the execution layer uses anything newer,
and nothing below it is platform-specific.

## Why the core is shaped this way

The reader consumes exactly one record per call, straight from a
`std::istream`. Three things follow:

- **Pipes and `std::cin` work.** Nothing seeks, and nothing needs to know
  the length of the input in advance.
- **Memory is O(longest record), not O(input).** A hundred-gigabyte
  instruction stream is read through one `Instruction`.
- **The first record is available before the rest of the input exists.** A
  producer and a consumer can run at the same time.

### Every field owns its own bytes

`Instruction` is a plain struct of a `std::vector<std::string>` and seven
`std::string`s. That is the whole type. There is no shared buffer, no
offsets, no capacity bookkeeping, and no private section.

This is the part that changed most from the C version, where the same design
needed a `storage` buffer, an `argv_slots` array, two capacities, and eight
stack offsets to resolve after `realloc` moved things. All of it existed to
answer *who owns these bytes*, and `std::string` answers that by
construction. Three consequences:

- An `Instruction` stays valid after the stream it came from is gone.
- One built by hand for `write_instruction` is not a special case with its
  own ownership rules — it is the same type, and there is nothing to free.
- Three error states disappeared outright. `NullArgument` and `NullField`
  cannot happen because a `std::string` cannot be null, and allocation
  failure is `std::bad_alloc` rather than a state every caller must check.

`Instruction::clear()` clears each string rather than assigning a fresh one,
so the buffers survive into the next read. A caller reading a whole stream
through one instruction settles into steady state without reallocating:

```cpp
aos::Instruction inst;

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
  `Instruction`.
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
InstState read_instruction(std::istream &in, Instruction &inst,
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
`Instruction` and nothing else — never a path, never a stream — so it stays
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
| empty `env_path` | inherit the caller's environment entirely | |
| non-empty `env_path` | a file of `KEY=VALUE` lines that **replaces** the environment | extending is the caller's job to express; replacing is the reproducible option, and it is the one that cannot be built out of the other |
| `env_path` line without `=` | `EnvFileFailed` | better to fail than to guess |
| empty `exit_path` | the status is discarded | |
| non-empty `exit_path` | truncated, then the decimal status and a newline | |
| status of a signalled child | `128 + signum` | shell convention, and it keeps the field one number on every platform |
| `extra` | ignored | |
| failure to spawn | a runtime error, and **nothing** is written to `exit_path` | see below |
| several instructions | sequential; a non-zero child does not stop the run, a runtime failure does | a child's status is data, and recording it is what `exit_path` is for |

### Telling "command not found" from "the command exited 127"

These have to stay distinguishable, and after `fork` the child has no way to
return anything but an exit status. So the child inherits the write end of a
pipe marked `FD_CLOEXEC` and writes which step failed and its `errno` if it
cannot get as far as `execvp`. A successful `execvp` closes the pipe, and
the parent reads zero bytes.

That is what makes `ChdirFailed` and `OpenStdoutFailed` distinct states
rather than a single opaque `SpawnFailed`, and it is why a child that
genuinely exits 127 is reported as a normal result while a missing command
is not.

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
`Instruction`, which is why it takes a non-const reference despite modifying
nothing. Containing the wart in one function is the most C++ can do about
it; the alternative is a `const_cast` at every call site.

### The instruction file is a trust boundary

Executing an instruction file runs arbitrary commands with the invoking
user's privileges. That is the intended purpose, not a flaw, but it means
write access to an instruction file is equivalent to code execution. Worth
stating plainly in the user-facing documentation.

## Eventual goal: a shared library

The intended end state is a `.so` / `.dll` that other programs link against.
Some constraints are already satisfied:

- **Namespacing.** Everything is in `namespace aos`, so nothing collides.
- **No internal symbols to leak.** Every helper in `inst.cpp` and `exec.cpp`
  is in an anonymous namespace.
- **The two limits.** The record budget is a runtime argument, and
  `inst_argv_max()` reports the value the library was built with.

What is still outstanding, and the C++ rewrite made one of these harder:

- **The C++ ABI is a much bigger commitment than the C one was.** Exporting
  `std::string` and `std::vector` across a library boundary ties consumers
  to the same compiler, standard library, and `_GLIBCXX_USE_CXX11_ABI`
  setting. A C++ shared library with this interface is really "a library for
  programs built exactly like this one". A genuinely stable boundary would
  need an `extern "C"` shim — which is roughly the C API this code replaced.
  Worth deciding before anyone links against it.
- **`Instruction`'s layout is ABI.** It is public and used by value, so
  adding a field after release requires a soname bump.
- **Error enums are append-only.** Consumers compile against numeric values,
  so new states may only be appended.
- **Visibility.** Building a shared object still wants
  `-fvisibility=hidden` plus an export macro, or a version script.

## Suggested order

1. Decide whether the shipped boundary is C++ or an `extern "C"` shim, since
   it determines whether the public types can stay as they are.
2. Windows `execute`, if both targets are still required.
3. Symbol visibility, before any release.
