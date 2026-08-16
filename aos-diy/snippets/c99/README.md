# C99 assembly snippets

This directory mirrors the C++ snippets with C99 and POSIX interfaces:

- the queue stores borrowed `void *` values and drains queued items on close;
- `AosCall` borrows its strings, so producers must keep them alive;
- the loop uses executor and observer function pointers plus context pointers;
- outcomes use a fixed-size reason buffer and explicit status tags;
- process launching consumes `AosCall` directly, sources its `env` file over a
  clean base, applies `cwd`, and retains raw `waitpid` status;
- atomic publication uses `mkstemp`, file `fsync`, `rename`, and directory
  `fsync`.

Compile and run:

```sh
cc -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic \
  -Wshadow -Wconversion -Werror c99/*.c -pthread -o /tmp/aos-diy-c99-test
/tmp/aos-diy-c99-test
```

The portable `pipe` + `fcntl(FD_CLOEXEC)` sequence has a descriptor-leak race if
other threads fork at exactly the same time. On Linux, replace it with
`pipe2(..., O_CLOEXEC)` when concurrent spawning is possible.
