# @NAME@ — ONE artifact that is both an executable and a shared library.
# `main` is linked as a .so (-shared -fPIC) carrying an embedded PT_INTERP and
# a custom entry point, so the kernel can run it directly AND other programs
# can link against it. @NAME@ is filled in by dcap (only this Makefile is patched).
ifeq ($(origin CC),default)
  CC := gcc
endif
NAME   := @NAME@
CFLAGS := -std=c11 -O2 -Wall -Wextra -Iinclude -fPIC
LDLIBS := -lm -lpthread
PREFIX ?= /usr/local
ENTRY  := dcap_main

# Dynamic loader path, embedded as PT_INTERP so ./main is runnable. Detected
# from a known binary; override if that fails:
#   make INTERP=/lib64/ld-linux-x86-64.so.2
ifeq ($(origin INTERP),undefined)
  INTERP := $(shell readelf -p .interp /bin/sh 2>/dev/null | grep -o '/[^ ]*ld-[^ ]*')
endif

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
SO  := lib/lib$(NAME).so

all: main $(SO)

%.o: %.c
	$(CC) $(CFLAGS) -DDCAP_INTERP='"$(INTERP)"' -c $< -o $@

# -shared makes it a library; -e makes it runnable; -soname makes it linkable.
main: $(OBJ)
	@test -n "$(INTERP)" || { echo "error: cannot detect the dynamic loader; retry with 'make INTERP=/lib64/ld-linux-x86-64.so.2'" >&2; exit 1; }
	$(CC) $(CFLAGS) -shared -Wl,-e,$(ENTRY) -Wl,-soname,lib$(NAME).so $(OBJ) -o $@ $(LDLIBS)
	@chmod +x $@

# Consumers link -Llib -l$(NAME); the .so is just a symlink to the one artifact.
$(SO): main
	@mkdir -p lib
	ln -sf ../main $@

run: main
	./main

test: test/test
	./test/test
test/test: test/test.c $(SO)
	$(CC) $(CFLAGS) test/test.c -Llib -l$(NAME) -Wl,-rpath,'$$ORIGIN/../lib' -o $@ $(LDLIBS)

debug:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -g -O0 -Wall -Wextra -Iinclude -fPIC" all

release: all

# One file lands in $(PREFIX)/lib; $(PREFIX)/bin/$(NAME) is a symlink to it.
install: all
	install -d $(PREFIX)/bin $(PREFIX)/lib $(PREFIX)/include/$(NAME)
	install -m755 main $(PREFIX)/lib/lib$(NAME).so
	ln -sf ../lib/lib$(NAME).so $(PREFIX)/bin/$(NAME)
	install -m644 include/*.h $(PREFIX)/include/$(NAME)/

fmt:
	clang-format -i $(wildcard src/*.c include/*.h test/*.c)

clean:
	rm -rf lib main test/test $(OBJ)

.PHONY: all run test debug release install fmt clean
