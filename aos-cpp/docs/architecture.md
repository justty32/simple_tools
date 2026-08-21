# Architecture

The implementation has four small layers:

- `inst` owns the instruction data type and validation states.
- `format` is the only layer that knows JSON and converts buffers to instructions.
- `exec` knows process creation, redirection, environment setup, waiting, and timeouts.
- `run` owns POSIX input, CLI diagnostics, and the parse-then-execute loop; `main`
  only calls it.

`format` and `exec` do not depend on each other. The reusable object library
contains `inst`, `format`, and `exec`; the executable adds `run` and `main`.

## Whole-input reading

The runner reads to EOF into one bounded buffer, parses and validates every
record, and only then starts the first command. This provides batch atomicity:
one malformed record prevents all side effects from the file. Reading standard
input first also prevents children from consuming instruction bytes.

The tradeoff is deliberate. Memory use is bounded by the whole-input limit,
not merely the longest record. FIFO and pipeline input is no longer incremental:
execution waits until the producer closes its output, so a long-lived producer
cannot feed commands continuously. Finite producers still work as batches.
