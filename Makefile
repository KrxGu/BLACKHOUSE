# Order Book Reconstruction - High Performance Build System
# Author: Krish Gupta
# Target: Blockhouse Quant Dev Assignment

CXX := g++
CXXFLAGS_BASE := -std=c++20 -Wall -Wextra -Wpedantic -Iinclude -Isrc
CXXFLAGS_RELEASE := $(CXXFLAGS_BASE) -O3 -march=native -flto -DNDEBUG -ffast-math
CXXFLAGS_DEBUG := $(CXXFLAGS_BASE) -g -O0 -DDEBUG -fsanitize=address -fsanitize=undefined
CXXFLAGS_PROFILE := $(CXXFLAGS_BASE) -O3 -march=native -g -DPROFILE_MODE

SRCDIR := src
TESTDIR := test
SOURCES := $(wildcard $(SRCDIR)/*.cpp)
TEST_SOURCES := $(wildcard $(TESTDIR)/*.cpp)

TARGET := reconstruct_mbp
TEST_TARGET := run_tests

# Default target
.PHONY: all release debug profile test clean

all: release

# Release build for maximum performance
release: CXXFLAGS = $(CXXFLAGS_RELEASE)
release: $(TARGET)

# Debug build with sanitizers
debug: CXXFLAGS = $(CXXFLAGS_DEBUG)
debug: $(TARGET)

# Profile build for performance analysis
profile: CXXFLAGS = $(CXXFLAGS_PROFILE)
profile: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Test target (optional for extra points)
test: CXXFLAGS = $(CXXFLAGS_DEBUG)
test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES) $(filter-out $(SRCDIR)/main.cpp, $(SOURCES))
	$(CXX) $(CXXFLAGS) -o $@ $^

# Performance benchmarking
bench: release
	@echo "Running performance benchmark..."
	@time ./$(TARGET) data/sample_mbo.csv > benchmark_output.csv
	@echo "Performance test completed. Check benchmark_output.csv for results."

# Memory profiling with valgrind
memcheck: debug
	valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all ./$(TARGET) data/mbo.csv > /dev/null

# Cleanup
clean:
	rm -f $(TARGET) $(TEST_TARGET) *.csv *.out

# Help target
help:
	@echo "Available targets:"
	@echo "  release  - Optimized build for submission"
	@echo "  debug    - Debug build with sanitizers"
	@echo "  profile  - Profile build for perf analysis"
	@echo "  test     - Run unit tests"
	@echo "  bench    - Performance benchmark"
	@echo "  memcheck - Memory leak detection"
	@echo "  clean    - Remove build artifacts"

# File dependencies
$(SRCDIR)/main.cpp: $(SRCDIR)/order_book.hpp $(SRCDIR)/csv_parser.hpp
$(SRCDIR)/order_book.cpp: $(SRCDIR)/order_book.hpp $(SRCDIR)/order.hpp 