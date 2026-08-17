# Architecture

## Layers

```text
┌─ main ──────────────────────────────────────────────────────┐
│  open a stream  ->  read records  ->  execute them          │
├─ execution ─────────────────────────────────────────────────┤
│  aos_inst_t  ->  a spawned process                          │
├─ core ──────────────────────────────────────────────────────┤
│  aos_inst_t                                                 │
│  read:   FILE *  ->  aos_inst_t   (one record per call)     │
│  write:  aos_inst_t  ->  FILE *                             │
└─────────────────────────────────────────────────────────────┘
```

The core is one module, `aos/inst.h` and `src/inst.c`. It moves one record
between a stream and an `aos_inst_t`, in both directions, and it is the only
parser in the project.

There is no file layer. A caller that wants every record in a file opens the
file and calls `aos_inst_read` until it returns `AOS_INST_EOF`.

## Why the core is shaped this way

The reader consumes exactly one record per call, straight from a `FILE *`.
Three things follow, and they are the reason for the shape:

- **Pipes and stdin work.** Nothing seeks, and nothing needs to know the
  length of the input in advance.
- **Memory is O(longest record), not O(input).** A hundred-gigabyte
  instruction stream is read through a buffer sized by its longest record.
- **The first record is available before the rest of the input exists.** A
  producer and a consumer can run at the same time.

### One buffer per instruction, owned by the instruction

`aos_inst_t` holds nine public fields — `argc`, `argv`, and seven strings —
and four private ones. Every public string points into a single `storage`
buffer that the instruction owns, and `argv` points into a single owned slot
array. Two allocations per record, not one per line.

Both allocations are kept across reads. A caller that reads a whole stream
through one `aos_inst_t` stops allocating entirely once the buffers have
reached the size of the longest record seen so far:

```c
aos_inst_t inst;
aos_inst_state state;

aos_inst_init(&inst);
while ((state = aos_inst_read(stream, &inst)) == AOS_INST_OK) {
    /* use inst; its strings are valid until the next read */
}
aos_inst_free(&inst);
```

Storage moves under `realloc` while a record is being read, so the reader
records the eight field positions as offsets and turns them into pointers
only once the eighth line is in. That is eight `size_t` on the stack, and it
is what removes the need to measure a record before parsing it.

### Why the strings are `const char *` even though the struct owns them

The same type has to serve both directions. The reader fills an
`aos_inst_t` that owns its memory; a caller of the writer wants to assign
string literals and pass the result straight in:

```c
aos_inst_t inst;
const char *argv[] = { "echo", "hi", NULL };

aos_inst_init(&inst);
inst.argc = 2U;
inst.argv = argv;
inst.stdin_path = "";           /* ... and the other six */
aos_inst_write(stream, &inst);
```

Making the fields `const char *` is what lets that second case be written at
all. Ownership lives entirely in the private `storage` and `argv_slots`
pointers, which stay `NULL` in a hand-built instruction — so `aos_inst_free`
on one frees nothing and is safe, rather than being a documented hazard.

## What this design trades away

Recorded here so it is a decision and not an oversight.

- **Random access across records.** There is no array of every instruction
  in a file, and no "instruction 47". A caller that needs to revisit records
  must keep what it needs, and the strings are only valid until the next
  read, so keeping means copying.
- **Retry after a short read.** A stream cannot be rewound, so
  `AOS_INST_INCOMPLETE` is terminal: the bytes are gone. The previous
  buffer-based parser could in principle be handed more data and asked
  again. Nothing in this project needed that, and a blocking `FILE *`
  already waits for a slow writer rather than reporting a short record.
- **Serializing into a caller's buffer.** The writer targets a `FILE *`
  only. Anything wanting bytes in memory needs a new entry point.
- **A per-record error index.** The caller counts records itself; it knows
  how many `aos_inst_read` calls succeeded before the failing one.

## Failure leaves nothing behind

Every public field of an `aos_inst_t` is cleared when a read begins and set
only once the record is complete. So on **every** failing return — including
`AOS_INST_EOF` — the instruction is empty: `argc == 0`, `argv == NULL`,
every string `NULL`. A caller cannot accidentally use a half-parsed record,
and the previous record never survives a failed read.

`split_argv` checks both the empty-argv case and the argument limit before
it writes the first NUL, so a rejected argv line is never left half-split.

The writer is symmetric: it validates the entire record before emitting a
byte, so a rejected instruction leaves the stream untouched. The one
asymmetry left is physical — an I/O error part-way through a write may leave
a partial record on the stream, and no validation can prevent that.

## The record budget

`aos_inst_read` grows its storage buffer to fit whatever it is reading, so
without a limit a single malformed line would allocate without bound. An
instruction file is a trust boundary (see below), so a budget always
applies:

```c
aos_inst_state aos_inst_read(FILE *, aos_inst_t *);                 /* default */
aos_inst_state aos_inst_read_max(FILE *, aos_inst_t *, size_t);     /* explicit */
```

The budget counts the eight lines plus the NUL that terminates each of them.
Exceeding it is `AOS_INST_TOO_LONG`. `AOS_INST_RECORD_MAX_BYTES` is the
default the convenience form passes, not a ceiling on the API — any positive
budget is valid, and zero is rejected as an invalid argument rather than
being read as "unlimited".

It is a **runtime parameter, not a macro**. That is deliberate: a
compile-time limit in a public header is a trap for a shared library, where
the value compiled into the library governs and a consumer that redefines
the macro changes only its own copy of the header. It is also why the test
build needs no `-D` override and therefore no separate object tree.

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

`main()` opens a stream, reads instructions from it, and executes them.

Execution should take an `aos_inst_t`, never a path or a stream. Kept that
way it stays testable without touching the filesystem, and it does not care
where the record came from.

### This is the first non-portable component

Everything up to here is plain C99 with no platform assumptions. Process
spawning is not:

- POSIX: `fork` / `execvp`, `dup2` for redirection, `chdir`, `waitpid`
- Windows: `CreateProcess` with `STARTUPINFO` handle redirection; there is
  no `fork`

Both targets are required. The platform split should be isolated behind one
thin internal interface — ideally a single function taking an `aos_inst_t`
and returning an exit status — so the core and the tests stay pure C99.

One wrinkle the type makes visible: `argv` is `const char **`, and
`execv`/`execvp` take `char *const *`. The cast is unavoidable in C and
belongs inside the POSIX spawn function, not in its callers.

### Semantics the format does not yet define

The parser treats these fields as opaque bytes. Execution cannot. Each of
the following is currently undefined and has to be decided before an
implementation can be written; they are listed here rather than guessed at.

- **Empty `stdin_path` / `stdout_path` / `stderr_path`** — inherit the
  parent's handle, or attach the null device? These differ observably.
- **Empty `cwd`** — inherit the parent's working directory?
- **Existing `stdout_path` / `stderr_path`** — truncate or append?
- **`env_path` file format** — the parser calls lines 7 and 8 opaque, so no
  format exists yet. Also: does it replace the parent environment entirely,
  or extend it? Empty means what?
- **`exit_path` contents** — decimal status plus newline? What is written
  when the child is killed by a signal on POSIX, where there is no exit
  code? Windows has no equivalent, so a portable encoding has to be chosen.
- **Failure to spawn** (command not found, `cwd` missing, redirection target
  unopenable) — is that recorded in `exit_path` like a normal exit, or is it
  a runtime error that stops the run?
- **Multiple instructions** — sequential or concurrent? Does a failing
  instruction stop the remaining ones?
- **`extra`** — reserved for what? Deciding now avoids a compatibility break
  later.

### The instruction file is a trust boundary

Executing an instruction file runs arbitrary commands with the invoking
user's privileges. That is the intended purpose, not a flaw, but it means
write access to an instruction file is equivalent to code execution. Worth
stating plainly in the user-facing documentation once the runtime exists.

## Eventual goal: a shared library

The intended end state is a `.so` / `.dll` with a public API that other
programs link against. Three of the constraints that are expensive to
retrofit are already satisfied:

- **Symbol prefix.** Every public function and type is `aos_inst_*` /
  `aos_inst_t`, so nothing lands in the global namespace under a name as
  collision-prone as `instruction_read`.
- **No internal symbols to leak.** Every helper in `src/inst.c` is `static`.
  There is no internal header and nothing outside the module to export.
- **The two limits.** The record budget is a runtime argument, and
  `aos_inst_argv_max()` reports the argv limit compiled into the library
  rather than making consumers trust their copy of the macro.

What is still outstanding:

- **`aos_inst_t` layout freezes at the first release.** The struct is public
  and used by value, so its layout is ABI — including the four private
  fields, which are private by documentation only. C offers no way to hide
  them without an opaque pointer and a heap allocation per instruction.
  Changing them after release requires a soname bump.
- **Error enums become append-only.** Consumers compile against numeric
  values, so new states may only be appended. Reordering or inserting values
  silently changes the meaning of a stored or transmitted code.
- **Visibility.** Building a shared object still wants `-fvisibility=hidden`
  plus an export macro, or a linker version script.

## Suggested order

1. The execution layer: settle the undefined semantics above, then one
   platform-split spawn function taking an `aos_inst_t`, then wire `main()`
   from stream to execution.
2. Before any shared-library release: symbol visibility, and a decision on
   whether `aos_inst_t` stays a by-value public struct.

Items that were on this list and are no longer needed: two-phase commit in
the parser, a separate streaming reader, moving the stream writer out of the
core, and retiring `instruction_scan()`. The single-module rewrite removed
the conditions that made each of them necessary.
