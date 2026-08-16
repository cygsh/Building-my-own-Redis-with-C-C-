# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -pthread
TARGET = redis-server

# Directories
SRC_DIR = src
BIN_DIR = bin

# Source files
PART1_SRCS = $(wildcard $(SRC_DIR)/part1/*.c)
PART2_SRCS = $(wildcard $(SRC_DIR)/part2/*.c)
ALL_SRCS = $(PART1_SRCS) $(PART2_SRCS)

# Binaries
PART1_BINS = $(patsubst $(SRC_DIR)/part1/%.c,$(BIN_DIR)/part1/%,$(PART1_SRCS))
PART2_BINS = $(patsubst $(SRC_DIR)/part2/%.c,$(BIN_DIR)/part2/%,$(PART2_SRCS))

# Default target: build everything
all: $(PART1_BINS) $(PART2_BINS)

# Compile Part 1 files
$(BIN_DIR)/part1/%: $(SRC_DIR)/part1/%.c
	@mkdir -p $(BIN_DIR)/part1
	$(CC) $(CFLAGS) $< -o $@

# Compile Part 2 files
$(BIN_DIR)/part2/%: $(SRC_DIR)/part2/%.c
	@mkdir -p $(BIN_DIR)/part2
	$(CC) $(CFLAGS) $< -o $@

# Run the latest version (Thread Pool)
run: $(BIN_DIR)/part2/ch16-threadpool
	./$(BIN_DIR)/part2/ch16-threadpool

# Clean binaries
clean:
	rm -rf $(BIN_DIR)

# Help
help:
	@echo "Available commands:"
	@echo "  make all          - Build all chapters"
	@echo "  make run          - Run the latest version (Thread Pool)"
	@echo "  make clean        - Remove all binaries"
	@echo ""
	@echo "Individual binaries in bin/part1/ and bin/part2/"
	@echo ""
	@echo "Examples:"
	@echo "  ./bin/part1/ch6-epoll      - Run epoll server"
	@echo "  ./bin/part2/ch14-ttl       - Run TTL server"
	@echo "  ./bin/part2/ch16-threadpool - Run Thread Pool server"

.PHONY: all run clean help
