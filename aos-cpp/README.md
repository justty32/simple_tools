# aos-cpp

`aos-cpp` is a POSIX command runner written in C++17. It reads JSON Lines,
validates the complete batch, then executes each command in order.

## Build

Set `VCPKG_ROOT`, then configure, build, and test:

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default --output-on-failure
```

The executable is `build/aos-cpp` with the default preset.

## Usage

Pass one instruction file, or omit it to read standard input:

```sh
aos-cpp jobs.jsonl
printf '%s\n' '{"argv":["echo","hello"]}' | aos-cpp
```

For example, `jobs.jsonl` can capture output and the child status:

```json
{"argv":["sh","-c","printf 'hello from aos-cpp\\n'"],"stdout":"hello.txt","exit":"hello.status"}
```

Running `aos-cpp jobs.jsonl` writes `hello from aos-cpp` to `hello.txt` and
`0` to `hello.status`. See [the format](docs/format.md), [execution semantics](docs/exec.md),
and [architecture](docs/architecture.md) for details.
