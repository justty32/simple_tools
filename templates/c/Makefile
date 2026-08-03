# Hybrid C project: builds an executable `main` and a library (.a + .so).
# @NAME@ is filled in by dcap (only this Makefile is patched).
ifeq ($(origin CC),default)
  CC := gcc
endif
NAME   := @NAME@
CFLAGS := -std=c11 -O2 -Wall -Wextra -Iinclude
LDLIBS := -lm -lpthread
PREFIX ?= /usr/local

LIB_SRC := $(wildcard src/lib*.c)
LIB_OBJ := $(LIB_SRC:.c=.o)
A_LIB   := lib/lib$(NAME).a
SO_LIB  := lib/lib$(NAME).so

all: main $(A_LIB) $(SO_LIB)

lib:
	@mkdir -p lib

$(A_LIB): $(LIB_OBJ) | lib
	ar rcs $@ $(LIB_OBJ)

$(SO_LIB): $(LIB_SRC) | lib
	$(CC) $(CFLAGS) -fPIC -shared $(LIB_SRC) -o $@ $(LDLIBS)

# executable links the static lib so ./main runs standalone
main: src/main.c $(A_LIB)
	$(CC) $(CFLAGS) src/main.c $(A_LIB) -o $@ $(LDLIBS)

run: main
	./main

test: test/test
	./test/test
test/test: test/test.c $(A_LIB)
	$(CC) $(CFLAGS) test/test.c $(A_LIB) -o $@ $(LDLIBS)

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -g -O0 -Wall -Wextra -Iinclude" all

release: all

# install: make a renamed copy first, install that, then delete the local copy
# (leaves `main` in the tree; never creates $(PREFIX)/bin/main)
install: all
	install -d $(PREFIX)/bin $(PREFIX)/lib $(PREFIX)/include/$(NAME)
	cp main $(NAME)
	install -m755 $(NAME) $(PREFIX)/bin/
	rm -f $(NAME)
	install -m644 $(A_LIB) $(SO_LIB) $(PREFIX)/lib/
	install -m644 include/*.h $(PREFIX)/include/$(NAME)/

fmt:
	clang-format -i $(wildcard src/*.c include/*.h test/*.c)

clean:
	rm -rf lib main test/test $(LIB_OBJ)

.PHONY: all lib run test debug release install fmt clean
