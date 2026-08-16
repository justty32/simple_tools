# C++20 assembly snippets

Language variants live in [`c99/`](c99/) and [`cpp11/`](cpp11/). The files in
this directory are the C++20 edition.

These files are intentionally separate from `src/`.  Copy only the pieces you
want, rename the namespace, and adapt the boundary types while assembling the
real implementation.

- `blocking_queue.hpp`: generic FIFO, multiple producers/consumers, drains on
  close, and wakes every blocked consumer.
- `call.hpp`: a C++20-only copy of the boundary shape, intentionally free of
  the project's C++23 `std::expected` declaration.
- `outcome.hpp`: keeps exit, signal, launch failure, rejection, and uncertainty
  distinct; `exit_number()` is only the lossy shell-facing projection.
- `event_loop.hpp`: the queue/executor seam plus exception containment and basic
  accounting.  It currently consumes the project's global `Call` type.
- `unique_fd.hpp`: move-only RAII for POSIX file descriptors.
- `posix_process.*`: consumes `Call` directly, sources its optional `env` file
  over a clean environment, applies `cwd`, performs direct `execve`, and keeps
  exec failure distinct from a program that exits with 127.
- `atomic_publish.*`: same-directory temporary file, file `fsync`, atomic
  `rename`, then directory `fsync`.

The process fragment deliberately leaves PATH lookup, timeouts, output limits,
and process-group cancellation outside its boundary. Those policies can be
layered on without making the fork/exec core harder to audit.

Compile and run the smoke test with:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
  -Wold-style-cast -Werror \
  snippets/snippet_test.cpp snippets/atomic_publish.cpp \
  snippets/posix_process.cpp -pthread -o /tmp/aos-diy-snippet-test
/tmp/aos-diy-snippet-test
```
