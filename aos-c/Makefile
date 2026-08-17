CXX = g++
CPPFLAGS = -Iinclude
CXXFLAGS = -std=c++11 -Wall -Wextra -Wpedantic
DEPFLAGS = -MMD -MP
LDFLAGS =
LDLIBS =

MODE ?= debug
BUILD_DIR = build/$(MODE)
TARGET_NAME = aos-c
TEST_TARGET_NAME = aos-c-tests
SOURCES = $(wildcard src/*.cpp)
OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
# 測試會連結程式所使用的相同物件檔，但不包含程式進入點。
# 記錄大小上限是 read_instruction 的執行期參數，因此測試版本不必覆寫巨集，
# 也就不需要另一套獨立的物件檔目錄。
LIB_OBJECTS = $(filter-out $(BUILD_DIR)/main.o,$(OBJECTS))
TEST_SOURCES = $(wildcard tests/*.cpp)
TEST_OBJECTS = $(patsubst tests/%.cpp,$(BUILD_DIR)/tests/%.o,$(TEST_SOURCES))
DEPS = $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)

ifeq ($(MODE),release)
CXXFLAGS += -O2 -DNDEBUG
else ifeq ($(MODE),strict)
CXXFLAGS += -O0 -g3 -Werror
else
CXXFLAGS += -O0 -g3
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

# 使用獨立的 MODE/BUILD_DIR，避免平行執行 make 時與 clean 相依項目產生競爭。
strict:
	$(MAKE) MODE=strict all test

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

$(TEST_TARGET): $(LIB_OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(LDFLAGS) $(LIB_OBJECTS) $(TEST_OBJECTS) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.cpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) -Itests $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR):
	$(MKDIR)

$(BUILD_DIR)/tests: | $(BUILD_DIR)
	$(MKDIR)

clean:
	-$(RMDIR) build

-include $(DEPS)

.PHONY: all debug release strict test clean
