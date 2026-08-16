CXX = g++
CXXFLAGS = -std=c++26 -Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wconversion

test: test/*.cpp test/*.hpp
	$(CXX) $(CXXFLAGS) test/*.cpp -o /tmp/test
	/tmp/test

strict: CXXFLAGS += -Werror
strict: test

sanitize: CXXFLAGS += -fsanitize=address,undefined -g
sanitize: test

debug: CXXFLAGS += -g -O0
debug: test/*.cpp test/*.hpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) test/*.cpp -o build/test_debug

.PHONY: test strict sanitize debug
