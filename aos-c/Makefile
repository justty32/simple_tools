CC = gcc
CPPFLAGS = -Iinclude
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic
DEPFLAGS = -MMD -MP

MODE ?= debug
BUILD_DIR = build/$(MODE)
TARGET_NAME = aos-c
SOURCES = $(wildcard src/*.c)
OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS = $(OBJECTS:.o=.d)

ifeq ($(MODE),release)
CFLAGS += -O2 -DNDEBUG
else
CFLAGS += -O0 -g3
endif

ifeq ($(OS),Windows_NT)
TARGET_NAME := $(TARGET_NAME).exe
RM = cmd /C del /Q
RMDIR = cmd /C rmdir /S /Q
MKDIR = cmd /C mkdir
else
RMDIR = rm -rf
MKDIR = mkdir -p
endif

TARGET = $(BUILD_DIR)/$(TARGET_NAME)

all: $(TARGET)

debug:
	$(MAKE) MODE=debug all

release:
	$(MAKE) MODE=release all

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR):
	$(MKDIR) $(subst /,\,$(BUILD_DIR))

clean:
	-$(RMDIR) build

-include $(DEPS)

.PHONY: all debug release clean
