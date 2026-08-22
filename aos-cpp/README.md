# aos-cpp

`aos-cpp` is a POSIX command runner written in C++17. It reads one JSON
instruction object or an array of instruction objects, validates the complete
document, then executes each command in order.

## Build

Set `VCPKG_ROOT`, then configure, build, and test:

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
```

The executable is `build/aos-cpp` with the default preset. The same build
produces `build/libaos.so` for the C and C++ library APIs.

## Usage

Pass one instruction file, or omit it to read standard input:

```sh
aos-cpp jobs.json
printf '%s\n' '{"argv":["echo","hello"]}' | aos-cpp
```

For example, `jobs.json` can run a batch and capture output and child status:

```json
[
  {"argv":["sh","-c","printf 'hello from aos-cpp\\n'"],"stdout":"hello.txt","exit":"hello.status"},
  {"argv":["printf","done\\n"]}
]
```

Running `aos-cpp jobs.json` writes `hello from aos-cpp` to `hello.txt` and
`0` to `hello.status`.

## Instruction files must be trusted

There are no input limits: no byte cap on a record or on the document, no
`argv` or `env` element cap, and no JSON nesting depth cap. Memory is bounded
by the caller's `ulimit` or cgroup, not by this program, and a deeply nested
document overflows the parser's stack — a crash, not an error status. An
instruction file is executable code and needs to be sourced like one.

See [the record format](docs/format.md), [execution semantics](docs/exec.md),
[the C++ API](docs/cxxapi.md), [the C API](docs/capi.md),
[the architecture](docs/architecture.md), and
[the design decisions](docs/decisions.md) for details.
