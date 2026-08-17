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

The executables are written to `build/debug/` and `build/release/`.

Run the debug build:

```powershell
.\build\debug\aos-c.exe
```

Remove all build output:

```powershell
C:\dev\mingw64\bin\mingw32-make.exe clean
```

## Layout

- `src/` contains C source files and the program entry point.
- `include/aos/` contains public headers.
- `docs/` contains project documentation.
