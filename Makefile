# Build dcap itself. No CMake - just g++ (C++20) + make.
# On Windows use `mingw32-make`; on UNIX use `make`.
ifeq ($(origin CXX),default)
  CXX := g++
endif
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
# Static-link libstdc++/libgcc/winpthread so dcap runs without MinGW on PATH.
LDFLAGS  := -static
SRC      := $(wildcard src/*.cpp)

ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

BIN := bin/dcap$(EXE)

all: $(BIN)

# templates/ files are #embed-ed by src/templates.cpp, so they are real
# prerequisites of the build.
$(BIN): $(SRC) $(wildcard templates/*/* templates/*)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

install: all
	cp $(BIN) /usr/local/bin/

clean:
	rm -rf bin

.PHONY: all install clean
