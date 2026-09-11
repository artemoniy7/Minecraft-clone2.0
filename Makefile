CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2

asm16.exe: asm16.cpp asm16_core.hpp
	$(CXX) $(CXXFLAGS) asm16.cpp -o $@

.PHONY: clean
clean:
	rm -f asm16.exe
