# aos-simple —— 零第三方相依，clone 下來就能建。
#
#   make            建 build/test
#   make test       跑測試
#   make strict     一整排嚴格警告
#   make sanitize   ASan + UBSan（SAN=thread 跑 ThreadSanitizer）
#   make clean

BUILD  ?= build
CXX    ?= c++
CXXFLAGS ?= -O2 -g

STD  := -std=c++17
WARN := -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wconversion

ALL_CXXFLAGS = $(STD) $(WARN) $(CXXFLAGS)
LDLIBS := -lpthread

SRCS := src/call.cpp src/queue.cpp src/loop.cpp src/router.cpp src/tracker.cpp src/priority_queue.cpp src/pool.cpp src/exec.cpp
OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(SRCS))

# 兩支測試是刻意分開的：
#   core  一個子行程都不起、一個檔案都不開
#   exec  真的 fork、真的寫檔
# 分開才講得出「核心不碰外面」這句話，也才知道一個失敗是哪一邊的。
# ext/ 是**擴充**：核心（src/）不認識它，它只用 src/ 的公開介面。
# 分開一組 object 就是為了讓「核心沒有依賴 ext」這件事在建置上也成立。
EXT_SRCS := ext/agent/agent.cpp ext/input/json.cpp ext/input/ndjson.cpp ext/input/socket.cpp
EXT_OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(EXT_SRCS))

TEST_BINS := $(BUILD)/test-core $(BUILD)/test-exec $(BUILD)/test-agent $(BUILD)/test-input $(BUILD)/test-socket
TEST_OBJS := $(BUILD)/test/test_core.o $(BUILD)/test/test_exec.o $(BUILD)/test/test_agent.o $(BUILD)/test/test_input.o $(BUILD)/test/test_socket.o
DEPS := $(OBJS:.o=.d) $(EXT_OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(BUILD)/bin/main.d

.PHONY: all test strict sanitize clean
all: $(BUILD)/aos-simple $(TEST_BINS)

$(BUILD)/test-core: $(OBJS) $(BUILD)/test/test_core.o
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/test-exec: $(OBJS) $(BUILD)/test/test_exec.o
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/test-agent: $(OBJS) $(EXT_OBJS) $(BUILD)/test/test_agent.o
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/test-input: $(OBJS) $(EXT_OBJS) $(BUILD)/test/test_input.o
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/test-socket: $(OBJS) $(EXT_OBJS) $(BUILD)/test/test_socket.o
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

# 真的跑得起來的那支。
$(BUILD)/aos-simple: $(OBJS) $(EXT_OBJS) $(BUILD)/bin/main.o
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -MMD -MP -c $< -o $@

test: all
	./$(BUILD)/test-core
	./$(BUILD)/test-exec
	./$(BUILD)/test-agent
	./$(BUILD)/test-input
	./$(BUILD)/test-socket

# 動過就跑一次。這幾個不是每次建置的成本。
strict:
	$(MAKE) BUILD=build-strict CXXFLAGS="-O2 -g -Werror" all
	./build-strict/test-core
	./build-strict/test-exec
	./build-strict/test-agent
	./build-strict/test-input
	./build-strict/test-socket

SAN ?= address,undefined
sanitize:
	$(MAKE) BUILD=build-san \
	        CXXFLAGS="-O1 -g -fsanitize=$(SAN) -fno-omit-frame-pointer" \
	        LDFLAGS="-fsanitize=$(SAN)" all
	./build-san/test-core
	./build-san/test-exec
	./build-san/test-agent
	./build-san/test-input
	./build-san/test-socket

clean:
	rm -rf build build-strict build-san

-include $(DEPS)
