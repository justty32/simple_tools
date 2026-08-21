# C API

The public C ABI is declared by `<aos/aos.h>`. The header is C99-compatible and
does not expose C++ types or headers. Link programs with the shared library
`aos` (version 0.1.0, ABI version 0).

## Lifetime and strings

`aos_instruction_new()` creates an opaque instruction and returns `NULL` if it
cannot allocate one. Pass the handle to `aos_instruction_free()` when finished;
passing `NULL` is allowed. `aos_instruction_clear()` restores all fields to their
defaults.

String getters return borrowed, NUL-terminated pointers. They remain valid until
the same instruction is changed, cleared, read into, or freed. An invalid handle
or index produces `NULL`. The argv and environment index order is stable until a
mutation; environment entries are ordered by key.

All setters copy their inputs. An environment key must be nonempty and must not
contain `=`. `timeout_ms == 0` means no timeout.

## Reading and writing

`aos_instruction_read_buffer()` parses exactly one JSON object from the supplied
bytes. `aos_instruction_read_fd()` reads through EOF, leaves the caller-owned fd
open, marks it close-on-exec, and then parses the bytes as one instruction. It
retries interrupted reads and enforces the default total and record limits.
Neither function uses `FILE *`.

Serialization uses a two-call size query:

```c
size_t size = 0;
char *data;

if (aos_instruction_write_buffer(inst, NULL, 0, &size) !=
    AOS_INST_BUFFER_TOO_SMALL)
    return 1;
data = malloc(size + 1);
if (data == NULL ||
    aos_instruction_write_buffer(inst, data, size + 1, &size) != AOS_INST_OK)
    return 1;
```

The returned byte count includes the final LF but excludes the convenience NUL
terminator. A buffer must therefore hold `size + 1` bytes. A size-query or
too-small call returns `AOS_INST_BUFFER_TOO_SMALL`, writes the required record
length to `size`, and does not modify the buffer.

## Execution

`aos_instruction_execute()` waits for the command and fills `aos_exec_result`.
`status` is the exit status, or `128 + signal`; `signalled` distinguishes signal
termination, `timed_out` reports a timeout initiated by the library, and `error`
contains `errno` for applicable library failures.

Do not call `aos_instruction_execute()` from a multithreaded process. It uses
`fork()`, and before `exec` the child calls `setenv()` and `execvp()`. Neither is
on POSIX's async-signal-safe list, so locks held by another thread at the time of
the fork can deadlock the child.

Separate instruction handles may be constructed and edited independently, but
the strings returned by getters must only be used while their handle is not
being mutated. The library does not add synchronization around a handle.

## States, limits, and ABI version

`aos_inst_state_string()` and `aos_exec_state_string()` return static diagnostic
strings. The `aos_inst_*_max()` functions expose the compiled limits.
`aos_version_string()` returns the library version.

Enum numeric values are ABI and are frozen: releases may append values but will
not renumber or reuse existing ones. Any exception raised inside the C++
implementation is caught at the C boundary and reported as an allocation-failed
state, or as `NULL` for pointer-returning functions.
