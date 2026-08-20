CXX = g++
CC = gcc
CPPFLAGS = -Iinclude
# -fvisibility=hidden 讓符號只有被 AOS_API 標記才會出現在函式庫表面，
# 不會因為忘了處理而外洩。
CXXFLAGS = -std=c++11 -Wall -Wextra -Wpedantic -fvisibility=hidden \
           -fvisibility-inlines-hidden
# C ABI 的測試刻意用 C 編譯器建置，且只給 -Iinclude：這是唯一能證明公開
# 標頭真的只需要一個 C 編譯器的方法。
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic
DEPFLAGS = -MMD -MP
LDFLAGS =
LDLIBS =

MODE ?= debug
BUILD_DIR = build/$(MODE)
TARGET_NAME = aos-c
TEST_TARGET_NAME = aos-c-tests

# 函式庫本體，兩道公開邊界共用；run.cpp 是程式的驅動迴圈，不屬於任何一道。
LIB_SOURCES = src/inst.cpp src/inst_format.cpp src/exec.cpp src/capi.cpp
SOURCES = $(wildcard src/*.cpp)
OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
LIB_OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(LIB_SOURCES))

TEST_CXX_SOURCES = $(wildcard tests/*.cpp)
TEST_C_SOURCES = $(wildcard tests/*.c)
TEST_OBJECTS = $(patsubst tests/%.cpp,$(BUILD_DIR)/tests/%.o,$(TEST_CXX_SOURCES)) \
               $(patsubst tests/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_C_SOURCES))
DEPS = $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d)

# 共享函式庫的版本。SOVERSION 只在 ABI 破壞時才進位。
VERSION = 0.1.0
SOVERSION = 0

ifeq ($(MODE),release)
CXXFLAGS += -O2 -DNDEBUG
CFLAGS += -O2 -DNDEBUG
else ifeq ($(MODE),strict)
CXXFLAGS += -O0 -g3 -Werror
CFLAGS += -O0 -g3 -Werror
else
CXXFLAGS += -O0 -g3
CFLAGS += -O0 -g3
endif

RMDIR = rm -rf
MKDIR = mkdir -p $@
SHARED_NAME = libaos.so.$(VERSION)
SHARED_LDFLAGS = -Wl,-soname,libaos.so.$(SOVERSION)
PICFLAG = -fPIC

TARGET = $(BUILD_DIR)/$(TARGET_NAME)
TEST_TARGET = $(BUILD_DIR)/$(TEST_TARGET_NAME)
SHARED_TARGET = $(BUILD_DIR)/$(SHARED_NAME)
PIC_OBJECTS = $(patsubst src/%.cpp,$(BUILD_DIR)/pic/%.o,$(LIB_SOURCES))

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

shared: $(SHARED_TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $@ $(LDLIBS)

$(TEST_TARGET): $(LIB_OBJECTS) $(TEST_OBJECTS)
	$(CXX) $(LDFLAGS) $(LIB_OBJECTS) $(TEST_OBJECTS) -o $@ $(LDLIBS)

# 連結交給 $(CXX)：即使公開邊界是 C，實作仍需要 C++ 執行期。
$(SHARED_TARGET): $(PIC_OBJECTS)
	$(CXX) -shared $(SHARED_LDFLAGS) $(LDFLAGS) $(PIC_OBJECTS) -o $@ $(LDLIBS)
	ln -sf $(notdir $@) $(BUILD_DIR)/libaos.so.$(SOVERSION)
	ln -sf $(notdir $@) $(BUILD_DIR)/libaos.so

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/pic/%.o: src/%.cpp | $(BUILD_DIR)/pic
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(PICFLAG) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.cpp | $(BUILD_DIR)/tests
	$(CXX) $(CPPFLAGS) -Itests $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.c | $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR):
	$(MKDIR)

$(BUILD_DIR)/tests: | $(BUILD_DIR)
	$(MKDIR)

$(BUILD_DIR)/pic: | $(BUILD_DIR)
	$(MKDIR)

clean:
	-$(RMDIR) build

-include $(DEPS)

.PHONY: all debug release strict test shared clean
