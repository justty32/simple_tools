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
# The tests link the same objects as the program, minus its entry point.
# The record budget is a runtime argument to aos_inst_read_max, so a test
# build needs no macro override and therefore no separate object tree.
LIB_OBJECTS = $(filter-out $(BUILD_DIR)/main.o,$(OBJECTS))
TEST_SOURCES = $(wildcard tests/*.c)
TEST_OBJECTS = $(patsubst tests/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_SOURCES))
DEPS = $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)

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

$(TEST_TARGET): $(LIB_OBJECTS) $(TEST_OBJECTS)
	$(CC) $(LDFLAGS) $(LIB_OBJECTS) $(TEST_OBJECTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.c | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR):
	$(MKDIR)

$(BUILD_DIR)/tests: | $(BUILD_DIR)
	$(MKDIR)

clean:
	-$(RMDIR) build

-include $(DEPS)

.PHONY: all debug release strict test clean
