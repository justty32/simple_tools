# C++11 assembly snippets

This directory preserves the C++ shape without relying on post-C++11 library
features:

- `OptionalString` replaces `std::optional<std::string>`;
- queue extraction uses an output parameter and `TakeResult`;
- the event loop retains RAII, virtual executors, observers, and exception
  containment;
- process and atomic-file code use plain strings instead of `filesystem` and
  `string_view`; process execution consumes `Call` directly, sources its `env`
  file over a clean base, and applies its `cwd` in the child.

Compile and run:

```sh
g++ -std=c++11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
  -Wold-style-cast -Werror cpp11/snippet_test.cpp cpp11/atomic_publish.cpp \
  cpp11/posix_process.cpp -pthread -o /tmp/aos-diy-cpp11-test
/tmp/aos-diy-cpp11-test
```

`posix_process.cpp` uses portable `pipe` + `fcntl(FD_CLOEXEC)`. If several
threads may fork concurrently on Linux, use `pipe2(..., O_CLOEXEC)` to remove
the small descriptor-leak race between those two calls.
