# Architecture

## Layers

The design is three layers, innermost first. Everything else follows from this
ordering.

```text
┌─ file layer ────────────────────────────────────────────────┐
│  many instructions in one file; ownership of buffers        │
│  instruction_file_read / instruction_file_free              │
├─ core ──────────────────────────────────────────────────────┤
│  instruction_t                                              │
│  parse:  char **ptr        -> instruction_t   (streamable)  │
│  write:  instruction_t     -> char **ptr                    │
└─────────────────────────────────────────────────────────────┘
```

The core knows nothing about files. It moves one record between a byte cursor
and an `instruction_t`, in both directions, and **must be usable on a stream** —
that is, it must work when the caller can only supply bytes a chunk at a time.

The file layer sits on top and adds exactly two things: reading many records,
and owning the memory they borrow from.

## Why the core is shaped this way

`instruction_t` is deliberately small (72 bytes on x64). It holds `argc`, a
borrowed `const char **argv`, and seven borrowed field pointers. It owns
nothing. Both the argv slot array and the character buffer belong to the caller.

That is what makes the core streamable: a caller with a single record-sized
buffer and a single slot array can parse an unbounded number of records through
it, reusing both.

The parser's real precondition is narrow:

> `*ptr` points at a writable, NUL-terminated byte range containing at least one
> complete eight-line record.

Never "the whole file".

## Consequences for the current code

Two things sit in the wrong layer today.

### `instruction_write(FILE *, ...)` is not core

The core writer is `instruction_write_buffer(char **ptr, size_t *remaining, ...)`,
which matches the layering. But `instruction_write.c` also exports

```c
instruction_write_state instruction_write(FILE *stream, const instruction_t *);
```

That is a stream-level operation living in the core module, and it duplicates
the serialization logic rather than building on the `char **` writer. By the
layering above it belongs outside the core — either in the file layer or in a
thin stream layer beside it.

### `instruction_scan()` is a file-layer concern that leaked into core

`instruction_scan()` lives in `instruction.c` but exists solely so the file
layer can pre-size its two metadata arrays. It is not part of "parse one record
from a cursor".

It is there for a real reason. Each `instruction_t.argv` points **into** the
shared `argv_slots` array, so growing that array with `realloc` while parsing
would dangle every `argv` handed out so far. Pre-measuring avoids storing
offsets and rewriting them at the end.

But that reason is an artifact of the file layer allocating one flat array up
front. A file layer built on a streaming core would accumulate records instead
and would not need the scan at all.

## The blocker: reads are not atomic on failure

A streaming core must support "not enough data yet, try again later". The
current parser cannot, because it mutates its input before discovering the
record is short, and advances `*ptr` as it goes:

```text
attempt 1 -- incomplete record (4 of 8 lines)
  before   cursor_offset=0  bytes=[a\nb\nc\nd\n]
  -> instruction has fewer than eight lines
  after    cursor_offset=8  bytes=[a\0b\0c\0d\0]   <-- rewritten, cursor moved

attempt 2 -- feeder appends the tail and retries from the start
  buffer   cursor_offset=0  bytes=[a\0b\0c\0d\0e\nf\ng\nh\n]
  -> instruction has fewer than eight lines        <-- fails forever
```

Line 0's LF is now a NUL, so `strchr` stops there and that record can never be
parsed again.

`TOO_FEW_LINES` is also inherently ambiguous between "the buffer ran out" and
"the file ran out". Only the feeder knows which. That is correct layering, not a
gap: the parser reports what it sees, the feeder decides what it means.

## Planned change: make the core symmetric

Split `split_lines()` into measure and commit phases. It already keeps a
`lines[]` array; add a parallel `lengths[]`, locate all eight line bounds
without writing anything, then write the NULs and advance `*ptr` only once all
eight are known.

`split_argv()` needs the same treatment. It runs after `split_lines()` has
committed and writes NULs over tabs, so `EMPTY_ARGV` or `TOO_MANY_ARGUMENTS`
would still leave a half-rewritten buffer. Counting tabs and validating both
limits before splitting keeps the contract to one sentence rather than "atomic
for one error code but not the others".

The guarantee to document on `instruction_read()`:

> On failure `*ptr` and the input buffer are both unchanged; the caller may
> retry after supplying more data.

This is not only a streaming enabler. The write direction **already** promises
that a validation or capacity failure leaves `*ptr`, `*remaining`, and the
destination untouched. Today the two halves of the core disagree; this makes
them symmetric.

Cost: eight `size_t` on the stack. `line_bound()` is already called once per
line, so no extra scanning is introduced.

## Sketch: the streaming reader

Once reads are atomic, this sits beside `instruction_file` rather than replacing
it:

```c
typedef struct instruction_reader instruction_reader;

/* Caller owns both buffers; the reader allocates nothing. */
void instruction_reader_init(instruction_reader *r, FILE *stream,
                             char *record_buf, size_t record_bytes,
                             const char **argv_slots, size_t slot_count);

/* Strings in *out borrow record_buf and stay valid until the next call. */
instruction_state instruction_reader_next(instruction_reader *r,
                                          instruction_t *out);
```

Memory becomes O(longest record) instead of O(file), the aggregate budget stops
being a limit at all, and the first record is available before the rest of the
input has been read.

Two things are genuinely new, and neither touches the parsing algorithm:

- A record longer than `record_bytes` needs its own error condition.
- Borrowed strings are valid only until the next call, so a caller that wants to
  retain a record must copy it.

Random access across all records is what gets traded away. That is what the file
layer is for.

## Note: the file layer cannot stream today

Independent of the above, `instruction_file_read()` takes a **path**, and
`instruction_file_load()` sizes the file with `fseek(SEEK_END)` / `ftell`. A pipe
or `stdin` therefore cannot be read at all — the seek fails and the call returns
`INSTRUCTION_FILE_SEEK_FAILED`. This is structural, not a bug, but it is worth
knowing before the file layer is expected to consume streams.

## What the instructions are for: execution

An instruction is a serialized process spawn. That is what every field is:

| line | role at execution time |
| --- | --- |
| `argv` | the command and its arguments |
| `stdin_path` / `stdout_path` / `stderr_path` | redirections |
| `exit_path` | where the exit status is written |
| `cwd` | working directory for the child |
| `env_path` | environment for the child |
| `extra` | reserved |

`main()` names a file, reads the instructions in it, and executes them.

### The execution layer depends on the core, not on the file layer

```text
┌─ main ──────────────────────────────────────────────────────┐
│  file layer  ->  execution layer                            │
├─ execution ─────────────────────────────────────────────────┤
│  instruction_t  ->  a spawned process                       │
├─ file layer ────────────────────────────────────────────────┤
│  many instructions in one file                              │
├─ core ──────────────────────────────────────────────────────┤
│  instruction_t, parse, write                                │
└─────────────────────────────────────────────────────────────┘
```

Execution should take an `instruction_t`, never a file path or a
`instruction_file_t`. Kept that way it works unchanged against the file layer
today and against the streaming reader later, and it stays testable without
touching the filesystem.

### This is the first non-portable component

Everything up to here is plain C99 with no platform assumptions. Process
spawning is not:

- POSIX: `fork` / `execvp`, `dup2` for redirection, `chdir`, `waitpid`
- Windows: `CreateProcess` with `STARTUPINFO` handle redirection; there is no
  `fork`

Both targets are required. The platform split should be isolated behind one thin
internal interface — ideally a single function that takes an `instruction_t` and
returns an exit status — so the core, the file layer, and the tests stay pure
C99 and portable.

### Semantics the format does not yet define

The parser treats these fields as opaque bytes. Execution cannot. Each of the
following is currently undefined and has to be decided before an implementation
can be written; they are listed here rather than guessed at.

- **Empty `stdin_path` / `stdout_path` / `stderr_path`** — inherit the parent's
  handle, or attach the null device? These differ observably.
- **Empty `cwd`** — inherit the parent's working directory?
- **Existing `stdout_path` / `stderr_path`** — truncate or append?
- **`env_path` file format** — the parser calls lines 7 and 8 opaque, so no
  format exists yet. Also: does it replace the parent environment entirely, or
  extend it? Empty means what?
- **`exit_path` contents** — decimal status plus newline? What is written when
  the child is killed by a signal on POSIX, where there is no exit code? Windows
  has no equivalent, so a portable encoding has to be chosen.
- **Failure to spawn** (command not found, `cwd` missing, redirection target
  unopenable) — is that recorded in `exit_path` like a normal exit, or is it a
  runtime error that stops the run?
- **Multiple instructions** — sequential or concurrent? Does a failing
  instruction stop the remaining ones?
- **`extra`** — reserved for what? Deciding now avoids a compatibility break
  later.

### The instruction file is a trust boundary

Executing an instruction file runs arbitrary commands with the invoking user's
privileges. That is the intended purpose, not a flaw, but it means write access
to an instruction file is equivalent to code execution. Worth stating plainly in
the user-facing documentation once the runtime exists.

## Eventual goal: a shared library

The intended end state is a `.so` / `.dll` with a public API that other programs
link against. That is later work, but some of its constraints are cheap to
satisfy now and expensive to retrofit, so they are recorded here.

### Symbol names need a prefix — cheapest to fix now

Public functions and types are currently unprefixed: `instruction_read`,
`instruction_t`, `instruction_state`, `instruction_file_read`. In a shared
library these land in a global symbol namespace where `instruction_read` is a
very plausible collision.

The macros already use `AOS_`; the functions and types do not. Renaming to
`aos_instruction_*` costs almost nothing today and becomes a breaking change for
every consumer once the library ships.

### Internal symbols will be exported unless stopped

`instruction_scan()` is declared in `src/instruction_internal.h` and is not
`static`, so it is a normal external symbol. Building a shared object as-is
would export it as part of the public ABI. This needs either
`-fvisibility=hidden` plus an explicit export macro, or a linker version script.

### Compile-time limits must become runtime values

Two limits are `#define`s in public headers:

- `AOS_INSTRUCTION_ARGV_MAX`
- `AOS_INSTRUCTION_FILE_MAX_BYTES`

Once the code is a shared library, the value compiled into the library is the
one that governs. A consumer that overrides the macro would change its own copy
of the header and nothing else, so the code would silently behave against a
different limit than the header appears to promise.

This is not hypothetical: the test build already overrides
`AOS_INSTRUCTION_FILE_MAX_BYTES` via `-D` precisely because it is compile-time.
That technique works only while the library and its caller are compiled
together.

Both should become runtime values before the library ships — a getter for the
argv limit, and a settable field or parameter for the memory budget.

### `instruction_t` layout freezes at the first release

The struct is public and used by value, so its layout is ABI. It has already
changed once during development (inline `argv[257]` to `const char **argv`).
That freedom ends when the library ships; after that, layout changes require a
soname bump.

### Error enums become append-only

Consumers compile against numeric values, so new states may only be appended.
Reordering or inserting values silently changes the meaning of stored or
transmitted codes.

## Suggested order

1. Two-phase commit in `instruction_read()`, with tests proving the buffer is
   untouched on every failure path and that a partial-then-completed record
   parses on retry.
2. `instruction_reader` on top of it.
3. Move `instruction_write(FILE *, ...)` out of the core, implemented over
   `instruction_write_buffer()` rather than duplicating serialization.
4. Optionally reimplement `instruction_file_read()` as stream-and-accumulate,
   which retires `instruction_scan()` and leaves exactly one parser.
5. The execution layer: settle the undefined semantics above first, then a
   single platform-split spawn function taking an `instruction_t`, then wire
   `main()` as file layer to execution layer.
6. Before any shared-library release: the `aos_` symbol prefix, symbol
   visibility, and moving the two compile-time limits to runtime.

Two notes on ordering. The renaming in item 6 is worth doing early rather than
last: every step above adds call sites, and each one makes the rename larger.
And item 5 does not depend on items 1 through 4 — execution consumes an
`instruction_t`, which already exists and is stable — so it can proceed in
parallel if that is the more interesting half.
