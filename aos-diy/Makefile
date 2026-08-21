CXX = g++
CXXFLAGS = -std=c++26 -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wconversion
TARGET := main
DEBUG_TARGET := build/main_debug
DEBUG_TEST_TARGET := build/test_debug
SRC := $(wildcard src/*.cpp)
HEADERS := $(wildcard src/*.hpp)

$(TARGET): $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET) $(DEBUG_TARGET) $(DEBUG_TEST_TARGET)

test: test/*.cpp test/*.hpp
	$(CXX) $(CXXFLAGS) test/*.cpp -o /tmp/test
	/tmp/test

strict: CXXFLAGS += -Werror
strict: test

sanitize: CXXFLAGS += -fsanitize=address,undefined -g
sanitize: test

debug: $(DEBUG_TARGET)

$(DEBUG_TARGET): $(SRC) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -g -O0 $(SRC) -o $(DEBUG_TARGET)

debug-test: $(DEBUG_TEST_TARGET)

$(DEBUG_TEST_TARGET): test/*.cpp test/*.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -g -O0 test/*.cpp -o $(DEBUG_TEST_TARGET)

.PHONY: clean test strict sanitize debug debug-test
