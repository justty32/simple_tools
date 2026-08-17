# aos-c

A small C99 project built with a plain Makefile.

## Requirements

- A C99 compiler such as GCC
- GNU Make

## Build

On Windows with MinGW-w64:

```powershell
C:\dev\mingw64\bin\mingw32-make.exe debug
C:\dev\mingw64\bin\mingw32-make.exe release
```

On POSIX platforms (Linux, macOS), just use `make`:

```sh
make debug
make release
```

The executables are written to `build/debug/` and `build/release/`.

Run the debug build:

```powershell
.\build\debug\aos-c.exe
```

Run the test suite:

```powershell
C:\dev\mingw64\bin\mingw32-make.exe test
```

Build and test with `-Werror` (fails on any warning):

```powershell
C:\dev\mingw64\bin\mingw32-make.exe strict
```

Remove all build output:

```powershell
C:\dev\mingw64\bin\mingw32-make.exe clean
```

## Layout

- `src/` contains C source files and the program entry point.
- `include/aos/` contains public headers.
- `tests/` contains standalone test sources (using the `CHECK` macro from `tests/test_check.h`, no framework), built and run via `make test`.
- `docs/architecture.md` describes the instruction format, the reader and
  writer in `include/aos/inst.h`, and what the design deliberately gives up.
