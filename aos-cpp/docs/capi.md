# C API

The public C ABI is declared by `<aos/aos.h>`. The header is C99-compatible,
contains no C++ types, and exposes an opaque `aos_instruction` handle.

## Building and linking

CMake produces link-time `libaos.so`, SONAME `libaos.so.0`, and the versioned
library `libaos.so.0.1.0`. Compile a client from the repository root with:

```sh
cc -std=c99 example.c -Iinclude -Lbuild -Wl,-rpath,"$PWD/build" -laos
```

`-laos` selects `libaos.so` while linking; the dynamic loader records and
loads its SONAME. The rpath above is convenient for an uninstalled build.
Installed or packaged clients should place `libaos.so.0` on the platform's
normal library search path instead.

## Complete example

```c
#include <aos/aos.h>

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    static const char record[] =
        "{\"argv\":[\"printf\",\"hello from C\\n\"],"
        "\"exit\":\"status.txt\"}";
    aos_instruction *inst = aos_instruction_new();
    aos_exec_result result;
    aos_inst_state istate;
    aos_exec_state estate;
    char *encoded = NULL;
    size_t needed = 0;
    int rc = 1;

    if (inst == NULL) {
        fputs("allocation failed\n", stderr);
        return 1;
    }

    istate = aos_instruction_read_buffer(record, sizeof record - 1, inst);
    if (istate != AOS_INST_OK) {
        fprintf(stderr, "parse: %s\n", aos_inst_state_string(istate));
        goto done;
    }

    estate = aos_instruction_execute(inst, &result);
    if (estate != AOS_EXEC_OK) {
        fprintf(stderr, "execute: %s (errno %d)\n",
                aos_exec_state_string(estate), result.error);
        goto done;
    }
    printf("child status=%d signalled=%d timed_out=%d\n",
           result.status, result.signalled, result.timed_out);

    istate = aos_instruction_write_buffer(inst, NULL, 0, &needed);
    if (istate != AOS_INST_BUFFER_TOO_SMALL) goto done;
    encoded = (char *)malloc(needed + 1);
    if (encoded == NULL) goto done;
    istate = aos_instruction_write_buffer(inst, encoded, needed + 1, &needed);
    if (istate != AOS_INST_OK) goto done;
    fputs(encoded, stdout);
    rc = 0;

done:
    free(encoded);
    aos_instruction_free(inst);
    return rc;
}
```

This parses one instruction, executes it, prints the result, serializes the
instruction, and frees every owned allocation.

## Lifetime and fields

`aos_instruction_new()` creates a default instruction and returns `NULL` on
allocation failure. `aos_instruction_clear()` restores defaults, and
`aos_instruction_free()` accepts `NULL`.

Use `aos_instruction_push_arg()` plus `aos_instruction_argc()` and
`aos_instruction_arg()` for argv. The five string fields are selected with
`AOS_FIELD_STDIN`, `STDOUT`, `STDERR`, `EXIT`, and `CWD` through the field
getter/setter. Environment entries use count/key/value getters and
`aos_instruction_set_env()`; setting an existing key replaces it. Keys are
ordered lexically, must be non-empty, and cannot contain `=`.
Timeout getter/setter values are milliseconds; zero disables the deadline.

All setters copy their strings. Getter strings are borrowed, NUL-terminated, and
remain valid only until that instruction is changed, cleared, read into, or
freed. Invalid handles, fields, and indices return the documented invalid state,
zero, or `NULL` according to return type.

## Reading, writing, and execution

`aos_instruction_read_buffer()` parses exactly one JSON value from the supplied
bytes. `aos_instruction_read_fd()` reads through EOF, leaves the caller-owned fd
open, marks it close-on-exec, retries interrupted reads, and parses the result as
one instruction. Both clear the destination before parsing and use the default
record/total limits. Neither accepts a multi-record JSON Lines batch or uses
`FILE *`.

Serialization uses a two-call size query, as in the example. The required byte
count includes the final LF but excludes the convenience NUL. A buffer must hold
`needed + 1` bytes. A query or too-small call returns
`AOS_INST_BUFFER_TOO_SMALL`, reports the required count, and leaves the buffer
untouched.

`aos_instruction_execute()` resets a non-NULL result, waits for the command, and
returns an `aos_exec_state`. Result `status` is the child exit value or
`128 + signal`; `signalled` marks signal termination, `timed_out` marks a
library-initiated timeout, and `error` carries `errno` for applicable API
failures. A child status such as 1, 126, or 127 still returns `AOS_EXEC_OK`.
See [execution semantics](exec.md) for the complete rules.

State-string functions return static diagnostic strings. The `aos_inst_*_max()`
functions expose compiled limits, and `aos_version_string()` reports the
library version. C++ exceptions never cross the C boundary: state-returning
operations map them to an allocation failure; other return shapes use `NULL` or
zero, and void cleanup operations suppress them.

## Threads

The API is safe to use from a multithreaded process. In particular, execution
merges the environment, allocates `argv`/`envp`, and resolves PATH entirely in
the parent before `fork`. The child then uses only async-signal-safe POSIX
operations until `execve`, avoiding allocator locks left by other threads.

Separate handles can be used concurrently. A handle has no internal lock: do not
mutate it concurrently, execute it while another thread mutates it, or retain a
borrowed getter pointer across a mutation. As with other process-wide APIs, the
caller must also synchronize concurrent changes to the process environment.

## ABI stability

The SONAME is the ABI boundary. Compatible releases retain `libaos.so.0`;
an incompatible change requires a new SOVERSION. While that SONAME is retained,
existing exported function signatures, public structure layout, and existing
enum numeric values do not change. Enum values are frozen and new values may
only be appended, never inserted by renumbering or reused.

New functions and appended enum values may be added. Opaque
`aos_instruction` internals, diagnostic text, implementation details, version
numbers, and queried resource limits may change without breaking the C ABI.
Compile-time `AOS_VERSION_*` macros describe the headers; use
`aos_version_string()` for the loaded library.
