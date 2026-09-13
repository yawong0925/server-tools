# ==============================================================================
# Makefile: mping build automation
# ==============================================================================

# Toolchain and Compilation Flags
CC       := gcc
CFLAGS   := -Wall -Wextra -O2 -pthread -Iinclude -MMD -MP
LDFLAGS  := -pthread

# Project Directory Layout
SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := build/obj
BIN_DIR  := bin

# Output Target Binary
TARGET   := $(BIN_DIR)/mping

# File Discoveries
SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
DEPS     := $(OBJS:.o=.d)

# Default Goal
all: $(TARGET)

# Final Linkage
$(TARGET): $(OBJS) | $(BIN_DIR)
	@echo "Linking executable: $@"
	$(CC) $^ -o $@ $(LDFLAGS)

# Compile C source files into object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "Compiling: $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Create required build output directories
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $@

# Include auto-generated header dependency rules (.d files)
-include $(DEPS)

# Clean compiled objects and output directories
clean:
	@echo "Cleaning build artifacts..."
	rm -rf build bin

# Install executable to standard system path
install: $(TARGET)
	@echo "Installing $(TARGET) to ~/apps/"
	@mkdir -p ~/apps/
	install -m 755 $(TARGET) ~/apps/ 
	cp ./mping.cfg ~/apps/mping.cfg

# Remove installed binary
uninstall:
	@echo "Uninstalling ~/apps/mping..."
	rm -f ~/apps/mping ~/apps/mping.cfg

.PHONY: all clean install uninstall