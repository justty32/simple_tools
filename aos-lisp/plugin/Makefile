# aos-lisp 外掛 —— 把 Janet 掛進 aos-core。
#
#   make                建 build/aos-lisp.so
#   make test           起一台真的 daemon 打它
#   make clean
#
# aos-core 一行都不用改：這是 .so 外掛，走 AOS_PLUGIN_ABI_VERSION 1 那個穩定介面。

BUILD        ?= build
AOS_CORE     ?= ../../aos-core
JANET_PREFIX ?= $(HOME)/.local

CXX      ?= c++
CXXFLAGS ?= -O2 -g

STD  := -std=c++17
WARN := -Wall -Wextra -Wpedantic -Wshadow
# janet.h 走 -isystem 而不是 -I：它用了 flexible array member，-Wpedantic 會對
# 每一個都叫一次。那是 janet 的事，不是我們的，而我們自己的程式碼還是要被嚴格看。
INC  := -I$(AOS_CORE)/include -isystem $(JANET_PREFIX)/include/janet

# 靜態連 libjanet.a：產物自足，不必管這台的 libjanet.so 有沒有進 ldconfig cache
# （它沒有）。實測 .a 是 PIC 編的，進得了 .so。
JANET_LIB := $(JANET_PREFIX)/lib/libjanet.a
LIBS      := $(JANET_LIB) -lm -ldl -lpthread -lrt

ALL_CXXFLAGS = $(STD) $(WARN) $(INC) -fPIC $(CXXFLAGS)

SRCS := plugin.cpp loop.cpp vm.cpp unixcall.cpp
OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

SO := $(BUILD)/aos-lisp.so

.PHONY: all clean test
all: $(SO)

$(SO): $(OBJS)
	$(CXX) -shared $^ $(LIBS) -o $@

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(ALL_CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

# aos-core 的 daemon 與 CLI。外掛測試要有它們才跑得起來。
$(AOS_CORE)/bin/aos-daemon $(AOS_CORE)/bin/aos:
	$(MAKE) -C $(AOS_CORE)

test: $(SO) $(AOS_CORE)/bin/aos-daemon $(AOS_CORE)/bin/aos
	./test.sh

clean:
	rm -rf $(BUILD)

-include $(DEPS)
