# aos-core —— 純 C99，除了 POSIX 之外沒有任何相依，所以這裡不需要 configure。
#
#   make                    建 bin/aos、bin/aos-daemon、範例外掛
#   make test               跑全部測試
#   make MODULE_SRCS=...    把 C++／C 子專案編進 daemon（見下面「編進來的模組」）
#   make clean
#
# 交叉編譯就照一般的方式覆寫：
#   make CC=arm-linux-gnueabihf-gcc
#
# clangd／VS Code 要的 compile_commands.json：
#   bear -- make            （bear 不是建置相依，只有要編輯器補全時才需要）

# 建置產物的位置。test-module 會用另一組，這樣兩種變體不會互相覆蓋。
BUILD    ?= build
BINDIR   ?= bin

CC       ?= cc
CXX      ?= c++
CFLAGS   ?= -O2 -g
CXXFLAGS ?= -O2 -g

# -std=c99 而不是 gnu99：讓非標準的東西編不過，才不會不小心用了 GCC 擴充。
# 代價是 POSIX 的東西預設被藏起來，所以要自己開 _POSIX_C_SOURCE，
# 不開的話症狀是 strdup／clock_gettime／pthread 全部「宣告不到」。
STD      := -std=c99
DEFS     := -D_POSIX_C_SOURCE=200809L
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
            -Wmissing-prototypes -Wwrite-strings
INC      := -Iinclude -Isrc
LDLIBS   := -lpthread -ldl

# 外掛用 dlsym 找自己的進入點，但它也可能回頭呼叫 aos_host_of 這類 host 的符號。
# -rdynamic 讓執行檔的符號進動態符號表，那些才連得到。
# **只有 daemon 需要**（CLI 不載外掛），所以跟一般的 LDFLAGS 分開，
# 這樣 make sanitize 之類覆寫 LDFLAGS 時兩邊都還是對的。
EXPORT_FLAGS := -rdynamic

ALL_CFLAGS   = $(STD) $(DEFS) $(WARN) $(INC) $(CFLAGS)
ALL_CXXFLAGS = $(DEFS) $(WARN_CXX) $(INC) $(CXXFLAGS)
# C++ 沒有 -Wstrict-prototypes／-Wmissing-prototypes（那是 C 專屬的）。
WARN_CXX := -Wall -Wextra -Wpedantic -Wshadow

# ── 要編進來的模組 ─────────────────────────────────────────────────
# 空的話 daemon 就是純 C，連 C++ runtime 都不會連進來。
# 加一個 C++ 子專案：
#     make MODULE_SRCS=examples/module-cpp/greet.cpp
# 平常想固定編進去就把它寫在這一行。細節見 src/modules.h。
MODULE_SRCS ?=

# 有 .cpp 就得用 C++ 連結（要 libstdc++），否則維持純 C 連結。
ifneq ($(filter %.cpp %.cc %.cxx,$(MODULE_SRCS)),)
  LINKER := $(CXX)
else
  LINKER := $(CC)
endif

# 範例模組要靠這個巨集才會被 modules.c 看見（見那個檔的說明）。
ifneq ($(filter examples/module-cpp/greet.cpp,$(MODULE_SRCS)),)
  DEFS += -DAOS_WITH_MODULE_GREET
endif

# ── 來源 ───────────────────────────────────────────────────────────
# 刻意分三組而不是全部混在一起：**CLI 不該連進 daemon 那半邊**。
# 混在一起的話 bin/aos 會需要 modules.c，而那個檔會去參照編進來的模組——
# CLI 根本沒有模組可言，卻會因此連不起來。
#
#   共用   訊框、協定、緩衝、socket 路徑
#   CLI    只有 client.c
#   daemon 命令樹、session、runtime、外掛與模組的載入
COMMON_SRCS := src/buf.c src/frame.c src/protocol.c src/socket_path.c
CLI_SRCS    := src/client.c
SERVER_SRCS := $(filter-out $(COMMON_SRCS) $(CLI_SRCS) src/main_cli.c \
                 src/main_daemon.c, $(wildcard src/*.c src/commands/*.c))

COMMON_OBJS := $(patsubst %,$(BUILD)/%.o,$(COMMON_SRCS))
CLI_OBJS    := $(patsubst %,$(BUILD)/%.o,$(CLI_SRCS))
SERVER_OBJS := $(patsubst %,$(BUILD)/%.o,$(SERVER_SRCS))
MODULE_OBJS := $(patsubst %,$(BUILD)/%.o,$(MODULE_SRCS))

BINS    := $(BINDIR)/aos $(BINDIR)/aos-daemon $(BINDIR)/plugins/hello.so
TESTS   := $(BUILD)/test_protocol $(BUILD)/test_session

# 平常不開，但每次要動這個專案之前值得跑一次（見 make strict / make analyze）。
# -Wconversion 抓的是隱式的窄化與有號無號互轉，那是 C 裡最安靜的一類錯。
STRICT_FLAGS := -Wconversion -Wsign-conversion -Wcast-qual -Wcast-align \
                -Wswitch-enum -Wundef -Wredundant-decls -Wold-style-definition \
                -Wdouble-promotion -Wformat=2 -Wnull-dereference \
                -Wstrict-overflow=2 -Wvla

.PHONY: all everything test test-module strict analyze sanitize clean
all: $(BINS)

# 執行檔加測試執行檔。strict／analyze／sanitize 都要連測試一起檢查，
# 不然覆蓋率會少掉一大塊。
everything: $(BINS) $(TESTS)

# ── 編譯規則 ───────────────────────────────────────────────────────
$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -MMD -MP -c $< -o $@

# ── 執行檔 ─────────────────────────────────────────────────────────
$(BINDIR)/aos: $(BUILD)/src/main_cli.c.o $(COMMON_OBJS) $(CLI_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BINDIR)/aos-daemon: $(BUILD)/src/main_daemon.c.o $(COMMON_OBJS) $(SERVER_OBJS) \
                $(MODULE_OBJS)
	@mkdir -p $(dir $@)
	$(LINKER) $^ $(LDFLAGS) $(EXPORT_FLAGS) $(LDLIBS) -o $@

# 外掛只 include 一個標頭、不連 aos 的任何函式庫，所以相依只有它自己。
$(BINDIR)/plugins/hello.so: examples/plugin-hello/hello.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -fPIC -shared $< -o $@

# ── 測試 ───────────────────────────────────────────────────────────
$(BUILD)/test_protocol: $(BUILD)/test/protocol_test.c.o $(COMMON_OBJS)
	$(CC) $^ $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD)/test_session: $(BUILD)/test/session_test.c.o $(COMMON_OBJS) \
                    $(SERVER_OBJS) $(MODULE_OBJS)
	$(LINKER) $^ $(LDFLAGS) $(LDLIBS) -o $@

test: $(BINS) $(TESTS)
	@echo "── protocol ──"   && ./$(BUILD)/test_protocol
	@echo "── session ──"    && ./$(BUILD)/test_session
	@echo "── end to end ──" && ./test/e2e.sh ./$(BINDIR)/aos-daemon ./$(BINDIR)/aos \
	                                          ./$(BINDIR)/plugins/hello.so
	@echo "全部通過"

# 把 C++ 範例模組編進去再測一次。用另一組建置目錄，這樣純 C 的產物不會被覆蓋。
# 這條之所以要獨立跑，是因為「編進來的模組」會改變 daemon 的連結方式
# （要 libstdc++），不該影響預設建置。
test-module:
	$(MAKE) BUILD=build-mod BINDIR=bin-mod \
	        MODULE_SRCS=examples/module-cpp/greet.cpp
	./test/module_test.sh ./bin-mod/aos-daemon ./bin-mod/aos

# ── 額外的檢查 ──────────────────────────────────────────────────────
# 這三個都是「改完之後跑一次」的東西，不是每次建置都要付的成本。

# 嚴格警告。目前應該是零，有東西冒出來就是真的值得看。
strict:
	$(MAKE) clean
	$(MAKE) BUILD=build-strict BINDIR=bin-strict \
	        CFLAGS="-O2 -g $(STRICT_FLAGS)" everything
	@echo "沒有警告就是過了"

# GCC 內建的靜態分析器。它會跨函式追指標與資源，抓得到警告抓不到的東西。
analyze:
	$(MAKE) BUILD=build-analyze BINDIR=bin-analyze CFLAGS="-O2 -g -fanalyzer" \
	        everything
	@echo "沒有 -Wanalyzer- 就是過了"

# 位址與未定義行為的執行期偵測。這個要真的跑測試才有意義。
# 併發的問題請另外跑 thread sanitizer：make sanitize SAN=thread
SAN ?= address,undefined
sanitize:
	$(MAKE) BUILD=build-san BINDIR=bin-san \
	        CFLAGS="-O1 -g -fsanitize=$(SAN) -fno-omit-frame-pointer" \
	        LDFLAGS="-fsanitize=$(SAN)" everything
	./build-san/test_protocol
	./build-san/test_session
	./test/e2e.sh ./bin-san/aos-daemon ./bin-san/aos ./bin-san/plugins/hello.so

clean:
	rm -rf build bin build-mod bin-mod build-strict bin-strict \
	       build-analyze bin-analyze build-san bin-san

# 每個 .o 旁邊的 .d 記著它 include 了哪些標頭，改標頭才會正確重編。
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
