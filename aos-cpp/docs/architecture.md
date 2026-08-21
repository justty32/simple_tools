# Architecture

The implementation has four deliberately narrow layers:

- `inst` owns `inst_t`, its public states, fixed structural limits, and clearing
  and state-string helpers. It knows neither bytes/JSON nor processes.
- `format` is the only layer that knows the JSON Lines schema. It converts byte
  buffers to validated instructions and instructions back to compact records;
  it knows nothing about `fork`, paths as operating-system resources, or CLI I/O.
- `exec` accepts one already-built `inst_t`. It owns environment and PATH
  preparation, `fork`/`execve`, redirection, process groups, waiting, timeouts,
  status-file output, and execution states; it never parses JSON or reads an
  instruction source.
- `run` owns POSIX input, CLI diagnostics, whole-batch parsing, and the
  sequential execution loop. `main` only delegates to it.

`format` and `exec` are siblings and do not depend on each other; both depend on
`inst`. The shared library contains `inst`, `format`, `exec`, spawn preparation,
and the C ABI. The executable adds `run` and `main`.

## Why the whole input is read first

The runner reads to EOF into one bounded buffer, then parses and validates every
record before starting the first command. The resulting guarantee is batch
atomicity for format errors: one malformed fifth record means records one
through four have not run. A streaming reader cannot offer that guarantee,
because the earlier commands' side effects are irreversible by the time the
fifth record is discovered. Reading stdin first also prevents a child that inherits
stdin from consuming bytes belonging to the instruction stream.

This choice has real costs. FIFO and pipeline input is not incrementally
executed: the first record waits until the producer closes its output, so a
long-lived producer cannot continuously feed this runner. Memory is bounded by
the entire input rather than by the longest record. The default 64 MiB total
limit is therefore a required resource boundary, not a cosmetic validation;
the separate 1 MiB record limit still rejects a single pathological line.

Finite producers remain useful as batches. The JSON Lines representation makes
records independently writable, but does not imply streaming execution.

## Work on each side of `fork`

In a multithreaded process, another thread may hold an allocator or library lock
at the instant `fork` duplicates the process. Only the calling thread survives
in the child, so a post-fork call that tries to acquire such a lock can deadlock
forever. POSIX therefore permits the child to call only async-signal-safe
operations until `exec` replaces the process image.

All allocation and complex preparation is performed in the parent before
`fork`: the implementation validates environment keys, merges the inherited
environment, materializes strings and `envp`, builds `argv`, obtains the
effective working directory, and resolves PATH candidates. The child receives
only stable pointers and calls `setpgid`, `open`, `dup2`, `close`, `chdir`,
`execve`, and `_exit`. These are async-signal-safe POSIX operations; no C++
allocation, `setenv`, string manipulation, or `execvp` occurs there. The parent
also calls `setpgid` to close the parent/child scheduling race before timeout
signals target the group.
