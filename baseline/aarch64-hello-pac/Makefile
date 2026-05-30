# PAC-enabled aarch64 hello-world build (ADR-2026-05-30-004).
# Builds on ADRs 001-003 (toolchain, target naming, Darwin/Linux runner split).

CC       = aarch64-unknown-linux-gnu-gcc
READELF  = aarch64-unknown-linux-gnu-readelf
OBJDUMP  = aarch64-unknown-linux-gnu-objdump
PAC_FLAGS := -mbranch-protection=pac-ret
CFLAGS   := -O0 -g -static -Wall -Wextra $(PAC_FLAGS)

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/hello
CORRUPT   := $(BUILD_DIR)/hello_corrupt
SRC       := hello.c
SRC_C     := hello_corrupt.c

# Host-aware runner selection (ADR-2026-05-30-003).
# macOS: Docker linux/arm64 (Apple Virtualization) exposes real armv8.3 PAC.
# Linux: qemu-aarch64 user-mode -- see ADR-2026-05-30-004 for PAC caveats.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    RUNNER         ?= docker run --rm --platform linux/arm64 -v $(CURDIR)/$(BUILD_DIR):/build alpine:3.20 /build/hello
    RUNNER_CORRUPT ?= docker run --rm --platform linux/arm64 -v $(CURDIR)/$(BUILD_DIR):/build alpine:3.20 /build/hello_corrupt
else
    RUNNER         ?= qemu-aarch64 $(TARGET)
    RUNNER_CORRUPT ?= qemu-aarch64 $(CORRUPT)
endif

.PHONY: all compile run clean verify verify-static verify-trap

all: compile

compile: $(TARGET) $(CORRUPT)

$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(CORRUPT): $(SRC_C) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: compile
	$(RUNNER); status=$$?; \
	if [ $$status -ne 0 ]; then echo "runner exited with status $$status" >&2; exit $$status; fi

# Static verification: paciasp/autiasp instructions in disassembly.
# Note: static binaries from this toolchain do not emit .note.gnu.property;
# the note is only used by the dynamic linker. Instruction presence is the
# authoritative check for static builds.
verify-static: compile
	@echo "[verify-static] checking for paciasp/autiasp in disassembly"
	@$(OBJDUMP) -d $(TARGET) | grep -E "paciasp|autiasp" \
	    || (echo "FAIL: no paciasp/autiasp instructions in disassembly" >&2; exit 1)
	@echo "[verify-static] OK"

# Runtime trap verification: corrupt LR after signing; expect SIGILL (exit 132 = 128+4).
verify-trap: compile
	@echo "[verify-trap] running hello_corrupt; expecting SIGILL (exit 132)"
	@$(RUNNER_CORRUPT); status=$$?; \
	if [ $$status -eq 132 ] || [ $$status -eq 135 ] || [ $$status -eq 139 ]; then \
	    echo "[verify-trap] OK (runner exited $$status: PAC authentication failed as expected)"; \
	elif [ $$status -eq 0 ]; then \
	    echo "FAIL: hello_corrupt exited 0; PAC is NOT enforced by the runner" >&2; exit 1; \
	else \
	    echo "WARN: hello_corrupt exited $$status (not 132/135/139); inspect manually" >&2; exit 1; \
	fi

verify: verify-static verify-trap

clean:
	rm -rf $(BUILD_DIR)
