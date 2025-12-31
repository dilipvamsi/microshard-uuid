# Compiler Config
CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -O2 -std=c99
CXXFLAGS = -Wall -Wextra -O2 -std=c++11

# Platform Checks
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    CFLAGS += -pthread
    CXXFLAGS += -pthread
endif

# Targets
TARGET_C = test_microshard_c.bin
TARGET_CPP = test_microshard_cpp.bin

.PHONY: all clean test test_c test_cpp

all: build_test_microshard_c build_test_microshard_cpp

# C Build
build_test_microshard_c:
	$(CC) $(CFLAGS) -o $(TARGET_C) test_microshard.c

# C++ Build
build_test_microshard_cpp:
	$(CXX) $(CXXFLAGS) -o $(TARGET_CPP) test_microshard.cpp

# Run All Tests
test: test_c test_cpp clean

test_c: build_test_microshard_c
	@echo "\n--- Running C Tests ---"
	@./$(TARGET_C)

test_cpp: build_test_microshard_cpp
	@echo "\n--- Running C++ Tests ---"
	@./$(TARGET_CPP)

clean:
	rm -f $(TARGET_C) $(TARGET_CPP)
