# Compiler and flags
CC = gcc
CFLAGS = -std=c99 \
         -D_POSIX_C_SOURCE=200809L \
         -D_XOPEN_SOURCE=700 \
         -Wall -Wextra -Werror \
         -Wno-unused-parameter \
         -fno-asm

# Directories
SRC_DIR = src

# Files
SRCS = $(SRC_DIR)/shell.c $(SRC_DIR)/commands.c $(SRC_DIR)/parser.c
TARGET = shell.out

# Default target
all: $(TARGET)

# Compile all sources directly into executable
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

# Clean
clean:
	rm -f $(TARGET)
