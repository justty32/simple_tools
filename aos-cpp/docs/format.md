# Record format

`aos-cpp` reads JSON Lines: each non-empty line is one JSON object. Writers emit
one compact object followed by LF. Readers also accept CRLF and skip empty lines.
The complete input must be valid before any command is started.

## Schema

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `argv` | array of strings | required | Command and arguments. It must be non-empty, and `argv[0]` must be non-empty. |
| `stdin` | string | `""` | Input file, or inherited standard input when empty. |
| `stdout` | string | `""` | Truncated output file, or inherited standard output when empty. |
| `stderr` | string | `""` | Truncated error file, or inherited standard error when empty. |
| `exit` | string | `""` | File receiving the decimal child status and LF; empty discards it. |
| `cwd` | string | `""` | Child working directory, or inherited directory when empty. |
| `env` | object of strings | `{}` | Values added to or overriding the inherited environment. |
| `timeout_ms` | unsigned integer | `0` | Maximum runtime in milliseconds; zero means unlimited. |

Unknown keys are errors. Environment keys cannot be empty or contain `=`.

## Limits and errors

By default, one record is limited to 1 MiB and the complete input to 64 MiB.
`argv` and `env` each allow at most 256 entries. JSON nesting is limited to a
depth of 3 while parsing.

Format errors include malformed JSON, a non-object record, unknown keys, wrong
field types, invalid or oversized `argv`/`env`, excessive nesting, and record or
input size violations. The CLI reports the physical, one-based line number and
returns status 1. No record from that input is executed.
