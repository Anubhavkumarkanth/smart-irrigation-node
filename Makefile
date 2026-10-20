CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -Ilib/irrigation

test: build/tests
	./build/tests

build/tests: test/test_main.cpp lib/irrigation/irrigation.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -rf build

.PHONY: test clean
