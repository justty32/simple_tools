CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic
DEPFLAGS = -MMD -MP
LDFLAGS =
LDLIBS =

MODE ?= debug
BUILD_DIR = build/$(MODE)
TARGET_NAME = aos-c
TEST_TARGET_NAME = aos-c-tests
SOURCES = $(wildcard src/*.c)
OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
LIB_SOURCES = $(filter-out src/main.c,$(SOURCES))
TEST_LIB_OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/testlib/%.o,$(LIB_SOURCES))
TEST_SOURCES = $(wildcard tests/*.c)
TEST_OBJECTS = $(patsubst tests/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_SOURCES))
DEPS = $(OBJECTS:.o=.d) $(TEST_LIB_OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)

# Test builds need a small file-size cap so boundary tests don't allocate
# the real 64 MiB production limit. Production builds are unaffected.
TEST_FILE_MAX_BYTES = 65536

ifeq ($(MODE),release)
CFLAGS += -O2 -DNDEBUG
else ifeq ($(MODE),strict)
CFLAGS += -O0 -g3 -Werror
else
CFLAGS += -O0 -g3
endif

ifeq ($(OS),Windows_NT)
TARGET_NAME := $(TARGET_NAME).exe
TEST_TARGET_NAME := $(TEST_TARGET_NAME).exe
RMDIR = cmd /C rmdir /S /Q
MKDIR = cmd /C mkdir $(subst /,\,$@)
else
RMDIR = rm -rf
MKDIR = mkdir -p $@
endif

TARGET = $(BUILD_DIR)/$(TARGET_NAME)
TEST_TARGET = $(BUILD_DIR)/$(TEST_TARGET_NAME)

all: $(TARGET)

debug:
	$(MAKE) MODE=debug all

release:
	$(MAKE) MODE=release all

# Own MODE/BUILD_DIR avoids racing a clean dependency under parallel make.
strict:
	$(MAKE) MODE=strict all test

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

$(TEST_TARGET): $(TEST_LIB_OBJECTS) $(TEST_OBJECTS)
	$(CC) $(LDFLAGS) $(TEST_LIB_OBJECTS) $(TEST_OBJECTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/testlib/%.o: src/%.c | $(BUILD_DIR)/testlib
	$(CC) $(CPPFLAGS) $(CFLAGS) -DAOS_INSTRUCTION_FILE_MAX_BYTES=$(TEST_FILE_MAX_BYTES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.c | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) -DAOS_INSTRUCTION_FILE_MAX_BYTES=$(TEST_FILE_MAX_BYTES) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR):
	$(MKDIR)

$(BUILD_DIR)/tests: | $(BUILD_DIR)
	$(MKDIR)

$(BUILD_DIR)/testlib: | $(BUILD_DIR)
	$(MKDIR)

clean:
	-$(RMDIR) build

-include $(DEPS)

.PHONY: all debug release strict test clean
