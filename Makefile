# ================================================================
#  hollowkernel — Makefile
#
#  Usage:
#    make          → build the binary
#    make clean    → remove build artifacts
#    make run      → build + run with no args (shows usage)
# ================================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -std=c11 -g \
            -D_GNU_SOURCE \
            -Iinclude

TARGET  := hollowkernel
BUILD   := build

# ── sources & objects ──────────────────────────────────────────
#
# find all .c files under src/ automatically.
# When we add scheduler/, ipc/ etc. later they get picked up
# without any changes to this Makefile.
#
SRCS := $(shell find src -name '*.c')

# Transform: src/foo/bar.c → build/foo/bar.o
OBJS := $(patsubst src/%.c, $(BUILD)/%.o, $(SRCS))

# ── targets ────────────────────────────────────────────────────

.PHONY: all clean run

all: $(BUILD)/$(TARGET)

# Link all object files into the final binary
$(BUILD)/$(TARGET): $(OBJS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@
	@echo ""
	@echo "  ✓  built → $(BUILD)/$(TARGET)"
	@echo ""

# Compile each .c → .o
# $< = the .c file, $@ = the .o file
$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
	@echo "  ✓  cleaned"

run: all
	./$(BUILD)/$(TARGET)