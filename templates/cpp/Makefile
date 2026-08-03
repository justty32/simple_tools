# Hybrid C++ project: builds an executable `main` and a library (.a + .so).
# @NAME@ is filled in by dcap (only this Makefile is patched).
ifeq ($(origin CXX),default)
  CXX := g++
endif
NAME     := @NAME@
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Iinclude
PREFIX   ?= /usr/local

LIB_SRC := $(wildcard src/lib*.cpp)
LIB_OBJ := $(LIB_SRC:.cpp=.o)
A_LIB   := lib/lib$(NAME).a
SO_LIB  := lib/lib$(NAME).so

all: main $(A_LIB) $(SO_LIB)

lib:
	@mkdir -p lib

$(A_LIB): $(LIB_OBJ) | lib
	ar rcs $@ $(LIB_OBJ)

$(SO_LIB): $(LIB_SRC) | lib
	$(CXX) $(CXXFLAGS) -fPIC -shared $(LIB_SRC) -o $@

# executable links the static lib so ./main runs standalone
main: src/main.cpp $(A_LIB)
	$(CXX) $(CXXFLAGS) src/main.cpp $(A_LIB) -o $@

run: main
	./main

test: test/test
	./test/test
test/test: test/test.cpp $(A_LIB)
	$(CXX) $(CXXFLAGS) test/test.cpp $(A_LIB) -o $@

debug:
	$(MAKE) clean
	$(MAKE) CXXFLAGS="-std=c++20 -g -O0 -Wall -Wextra -Iinclude" all

release: all

# install: make a renamed copy first, install that, then delete the local copy
# (leaves `main` in the tree; never creates $(PREFIX)/bin/main)
install: all
	install -d $(PREFIX)/bin $(PREFIX)/lib $(PREFIX)/include/$(NAME)
	cp main $(NAME)
	install -m755 $(NAME) $(PREFIX)/bin/
	rm -f $(NAME)
	install -m644 $(A_LIB) $(SO_LIB) $(PREFIX)/lib/
	install -m644 include/*.hpp $(PREFIX)/include/$(NAME)/

fmt:
	clang-format -i $(wildcard src/*.cpp include/*.hpp test/*.cpp)

clean:
	rm -rf lib main test/test $(LIB_OBJ)

.PHONY: all lib run test debug release install fmt clean
