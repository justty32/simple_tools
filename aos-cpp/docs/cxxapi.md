# C++ API

The public C++17 interface is declared by `<aos/inst.hpp>`,
`<aos/format.hpp>`, and `<aos/exec.hpp>` in namespace `aos`.

## Types

`inst_t` is one instruction. Its public members are `argv` (a
`std::vector<std::string>`), `stdin_path`, `stdout_path`, `stderr_path`,
`exit_path`, and `cwd` (strings), `env` (a
`std::map<std::string,std::string>`), and `timeout_ms` (`std::uint64_t`,
default zero). `clear()` restores all defaults.

`InstState` reports format/validation results: `Ok`, `InvalidArgument`,
`JsonSyntax`, `NotAnObject`, `UnknownKey`, `FieldTypeMismatch`, `EmptyArgv`,
`TooManyArgs`, `TooManyEnv`, `EnvKeyInvalid`, `DepthExceeded`,
`RecordTooLong`, and `TotalTooLong`.

`ReadOptions` supplies `max_record_bytes` (default 1 MiB) and
`max_total_bytes` (default 64 MiB). `ExecState` contains `Ok`,
`InvalidArgument`, `SpawnFailed`, `WaitFailed`, and `ExitWriteFailed`.
`ExecResult` contains the reported `status`, `signalled`, `timed_out`, and an
`error` holding `errno` for applicable API failures. See [execution
semantics](exec.md) for the distinction between child statuses and API errors.

## Functions

`read_all(data, size, out, error_line, options)` parses a complete JSON Lines
buffer. It clears `out` first and publishes records only if every non-empty line
is valid. On a record error, optional `error_line` receives its physical,
one-based line number; it is initialized to zero, which remains the value for an
invalid pointer or a whole-buffer size failure. Empty input succeeds with an
empty vector.

`read_one(line, size, out, options)` parses exactly one JSON value from the byte
range. It clears `out` before checking or parsing and enforces both configured
byte limits. It does not skip an empty buffer or remove CR/LF; those are batch
framing behaviors of `read_all`.

`write_one(inst, out)` validates `inst`, serializes compact JSON with one final
LF, and appends it to `out`. On validation failure it appends nothing. Defaulted
optional fields are omitted.

`execute(inst, result)` resets `result`, validates non-empty `argv`, prepares
and runs one child, waits, optionally writes its status file, and returns an
`ExecState`. A nonzero child status still returns `ExecState::Ok`. The non-const
instruction supplies stable mutable character storage for `argv`; callers must
not mutate it during execution.

`to_string(InstState)` and `to_string(ExecState)` return pointers to static,
NUL-terminated diagnostic strings and are `noexcept`. `max_args()`,
`max_env()`, and `max_json_depth()` return the compiled limits (currently 256,
256, and 3) and are also `noexcept`.

Parsing, serialization, and execution preparation use allocating C++
containers. Allocation failures and other unexpected C++ exceptions are not
translated by this interface and may propagate to the caller. Output guarantees
above apply to returned `InstState` validation failures, not to an exception.

## Example

```cpp
#include <aos/exec.hpp>
#include <aos/format.hpp>

#include <cstring>
#include <string>
#include <vector>

int main() {
    const char input[] =
        "{\"argv\":[\"printf\",\"hello\\n\"],\"exit\":\"status.txt\"}\n";
    std::vector<aos::inst_t> jobs;
    std::size_t line = 0;
    if (aos::read_all(input, std::strlen(input), jobs, &line) !=
        aos::InstState::Ok) return 1;

    for (auto &job : jobs) {
        aos::ExecResult result;
        if (aos::execute(job, result) != aos::ExecState::Ok) return 2;
        if (result.status != 0) return result.status;
    }

    std::string encoded;
    return aos::write_one(jobs.front(), encoded) == aos::InstState::Ok ? 0 : 3;
}
```

Build against the shared library, for example:

```sh
c++ -std=c++17 example.cpp -Iinclude -Lbuild -Wl,-rpath,"$PWD/build" -laos
```

## Threads and choosing an ABI

Independent `inst_t` objects can be used concurrently. `execute` is suitable in
a multithreaded process because all allocating environment and PATH preparation
occurs before `fork`; the child uses only async-signal-safe operations until
`execve`. There is no internal synchronization for sharing one instruction,
output string/vector, or borrowed application state: do not mutate the same
objects concurrently, and do not change an instruction while it executes.

The C++ API and C API expose the same instruction, format, execution result, and
limits. Prefer C++ for direct container access, configurable `ReadOptions`, and
atomic multi-record `read_all`. Prefer the C API for opaque-handle ownership,
explicit allocation-failure states, C or other FFI languages, or a stable binary
boundary.

The C++ interface does **not** guarantee ABI compatibility across releases. Its
exported signatures and layouts contain `std::string`, `std::vector`,
`std::map`, and compiler-specific C++ ABI details. Code crossing compiler,
standard-library, language, or version boundaries should use `<aos/aos.h>` and
the C ABI instead.
