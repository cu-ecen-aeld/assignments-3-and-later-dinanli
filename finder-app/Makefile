# Makefile for writer application

# Compiler and flags
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Werror -g

# Target executable
TARGET := writer

# Source files
SRC := writer.c
OBJ := $(SRC:.c=.o)

# Default target: build writer
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean target: remove executable and object files
clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean

