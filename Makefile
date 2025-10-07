# Simple Makefile for ls-v1.0.0 project

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -std=c11

# Target executable
TARGET = bin/ls

# Source files
SRC = src/ls-v1.0.0.c

# Build target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

# Clean build artifacts
clean:
	rm -f $(TARGET)
