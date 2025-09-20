# server/Makefile

TARGET := aesdsocket
SRC := aesdsocket.c

# Allow cross compilation: e.g., CROSS_COMPILE=aarch64-linux-gnu-
CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -Wextra -Werror -pedantic -std=c11

all: $(TARGET)
default: all

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	$(RM) $(TARGET)
