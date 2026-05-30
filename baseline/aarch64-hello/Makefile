# Baseline aarch64 hello-world build
# See ADR-2026-05-25-001, ADR-2026-05-30-002, ADR-2026-05-30-003 for rationale.

CC      ?= aarch64-unknown-linux-gnu-gcc
CFLAGS  := -O0 -g -static -Wall -Wextra

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/hello
SRC       := hello.c

# Host-aware runner selection (ADR-2026-05-30-003).
# macOS: no qemu-user package exists; use Docker linux/arm64 (Apple Virtualization).
# Linux: qemu-aarch64 user-mode (ADR-001).
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    RUNNER ?= docker run --rm --platform linux/arm64 -v $(CURDIR)/$(BUILD_DIR):/build alpine:3.20 /build/hello
else
    RUNNER ?= qemu-aarch64 $(TARGET)
endif

.PHONY: all compile run clean

all: compile

compile: $(TARGET)

$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: compile
	$(RUNNER); \
	status=$$?; \
	if [ $$status -ne 0 ]; then \
		echo "runner exited with status $$status" >&2; \
		exit $$status; \
	fi

clean:
	rm -rf $(BUILD_DIR)
