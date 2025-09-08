############## LLM Generated Code Begins ##############
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
OBJ_DIR = obj


# Files
SRCS = $(SRC_DIR)/shell.c $(SRC_DIR)/commands.c $(SRC_DIR)/parser.c
OBJS = $(OBJ_DIR)/shell.o $(OBJ_DIR)/commands.o $(OBJ_DIR)/parser.o
TARGET = shell.out

# Default target
all: $(TARGET)

# Link step
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

# Compile step
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ensure obj/ exists
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
############## LLM Generated Code Ends ################
