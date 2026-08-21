# Record format

`aos-cpp` reads JSON Lines: each non-empty physical line is one JSON object.
Writers emit one compact object followed by LF. Readers accept LF or CRLF, and
also accept a final record without a line ending. A zero-length line is skipped;
after removing the CR from CRLF, that line is empty too. A whitespace-only line
is not empty and is parsed (and normally rejected) as a record.

The input is not one JSON array. JSON Lines makes records independently
generatable and keeps a physical line number for diagnostics without requiring
an outer document to be rewritten. More importantly, `aos-cpp` still reads to
EOF and validates the complete batch before execution, so this convenient wire
format does not weaken batch validation.

## Schema

| Key | JSON type | Required | Default | Meaning |
| --- | --- | --- | --- | --- |
| `argv` | array of strings | yes | none | Command followed by its arguments. The array and `argv[0]` must be non-empty. |
| `stdin` | string | no | `""` | File opened read-only as standard input; empty inherits the caller's stdin. |
| `stdout` | string | no | `""` | File created if needed and truncated as standard output; empty inherits stdout. |
| `stderr` | string | no | `""` | File created if needed and truncated as standard error; empty inherits stderr. |
| `exit` | string | no | `""` | File created/truncated after the child finishes and given decimal status plus LF; empty discards it. |
| `cwd` | string | no | `""` | Child working directory; empty inherits the caller's directory. A relative value starts from the caller's directory. |
| `env` | object, string values | no | `{}` | Overrides or adds variables on top of the inherited environment; unmentioned variables remain. |
| `timeout_ms` | unsigned integer | no | `0` | Runtime limit in milliseconds; zero waits without a deadline. |

Environment keys must be non-empty and cannot contain `=`. JSON object keys are
unique in the in-memory instruction; repeated source keys are handled by the
JSON parser rather than providing an ordered override mechanism.

This complete record runs `sh` under `/tmp`, supplies an environment variable,
redirects all three standard streams, records the status, and imposes a
five-second limit:

```json
{"argv":["sh","-c","read line; printf '%s: %s\\n' \"$LABEL\" \"$line\"; printf 'diagnostic\\n' >&2"],"stdin":"/tmp/aos-input.txt","stdout":"/tmp/aos-output.txt","stderr":"/tmp/aos-error.txt","exit":"/tmp/aos-status.txt","cwd":"/tmp","env":{"LABEL":"worker"},"timeout_ms":5000}
```

The referenced `/tmp/aos-input.txt` must already exist. On success this example
writes the other three `/tmp/aos-*` files named in the record.

## Validation states and limits

| Condition | `InstState` / C state |
| --- | --- |
| Null input pointer | `InvalidArgument` / `AOS_INST_INVALID_ARGUMENT` |
| Invalid JSON, including an empty single-record buffer | `JsonSyntax` / `AOS_INST_JSON_SYNTAX` |
| Top-level value is not an object | `NotAnObject` / `AOS_INST_NOT_AN_OBJECT` |
| Key outside the schema | `UnknownKey` / `AOS_INST_UNKNOWN_KEY` |
| Wrong field type, non-string argument, or non-string environment value | `FieldTypeMismatch` / `AOS_INST_FIELD_TYPE_MISMATCH` |
| Missing/empty `argv`, or empty `argv[0]` | `EmptyArgv` / `AOS_INST_EMPTY_ARGV` |
| More than 256 arguments | `TooManyArgs` / `AOS_INST_TOO_MANY_ARGS` |
| More than 256 environment entries | `TooManyEnv` / `AOS_INST_TOO_MANY_ENV` |
| Empty environment key or a key containing `=` | `EnvKeyInvalid` / `AOS_INST_ENV_KEY_INVALID` |
| More than 3 nested object/array levels while parsing | `DepthExceeded` / `AOS_INST_DEPTH_EXCEEDED` |
| One physical record exceeds `max_record_bytes` (default 1 MiB) | `RecordTooLong` / `AOS_INST_RECORD_TOO_LONG` |
| Entire supplied buffer exceeds `max_total_bytes` (default 64 MiB) | `TotalTooLong` / `AOS_INST_TOTAL_TOO_LONG` |

The depth check occurs during parsing, before a deeply nested document can be
fully built. The C++ API can replace the byte limits with `ReadOptions`; the CLI
and C API use the defaults. For a batch error, the CLI prints its physical,
one-based line number and returns 1. No record in that batch executes.

Unknown keys are deliberately rejected, not ignored. Otherwise an older binary
could silently run a record containing a newer safety field such as
`timeout_ms` with no timeout at all. Rejection also turns a typo such as
`"stdou"` into an explicit failure instead of silently losing redirection.

`write_one` validates the whole instruction before appending anything. It emits
only non-default optional fields, compact JSON, and one final LF.
