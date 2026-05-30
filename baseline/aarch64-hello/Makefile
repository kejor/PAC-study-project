# Baseline aarch64 hello-world build
# See ADR-2026-05-25-001 for rationale.

CC      ?= aarch64-unknown-linux-gnu-gcc
QEMU    ?= qemu-aarch64
CFLAGS  := -O0 -g -static -Wall -Wextra

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/hello
SRC       := hello.c

.PHONY: all compile run clean

all: compile

compile: $(TARGET)

$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: compile
	$(QEMU) $(TARGET); \
	status=$$?; \
	if [ $$status -ne 0 ]; then \
		echo "qemu-aarch64 exited with status $$status" >&2; \
		exit $$status; \
	fi

clean:
	rm -rf $(BUILD_DIR)
