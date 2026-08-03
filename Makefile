# Build dcap itself. No CMake — just g++ (C++20) + make. POSIX-oriented.
ifeq ($(origin CXX),default)
  CXX := g++
endif
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra
SRC      := $(wildcard src/*.cpp)
BIN      := bin/dcap
# templates/ files are #embed-ed by src/builtin_*.cpp, so they are build inputs.
TEMPLATE_FILES := $(shell find templates -type f)

all: $(BIN)

$(BIN): $(SRC) $(TEMPLATE_FILES)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

clean:
	rm -rf bin

.PHONY: all clean
