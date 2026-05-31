# PAC vulnerability test suite (ADR-2026-05-30-005, expanded by -006, -007).
# Builds seven self-contained vulnerability programs in two variants each
# (nopac, pac) and drives a Python harness that checks expected outcomes.

CC      = aarch64-unknown-linux-gnu-gcc
OBJDUMP = aarch64-unknown-linux-gnu-objdump

# -fno-stack-protector is MANDATORY: with the canary on, __stack_chk_fail
# would catch t01/t02/t04 overflows before PAC has a chance to, masking
# what we're trying to measure. See ADR Implementation Notes.
# -z execstack is included only to document that DEP is not the gating
# mitigation; none of the tests actually execute stack data.
COMMON_CFLAGS := -O0 -g -static -no-pie -Wall -Wextra \
                 -fno-stack-protector -Wl,-z,execstack
NOPAC_CFLAGS  := $(COMMON_CFLAGS)
PAC_CFLAGS    := $(COMMON_CFLAGS) -mbranch-protection=pac-ret

BUILD_DIR := build
COMMON    := common/win.c

# Per-test source maps (vuln.c only; common/win.c is appended below).
SRCS_t01_stack_bof_ret    := t01_stack_bof_ret/vuln.c
SRCS_t02_ret2func         := t02_ret2func/vuln.c
SRCS_t03_rop_2gadget      := t03_rop_2gadget/vuln.c t03_rop_2gadget/gadgets.c
SRCS_t04_off_by_one_lr    := t04_off_by_one_lr/vuln.c
SRCS_t05a_fptr_overwrite  := t05_negative_controls/fptr_overwrite.c
SRCS_t05b_data_only       := t05_negative_controls/data_only.c
SRCS_t06_sp_collision_pac_reuse := t06_sp_collision_pac_reuse/vuln.c
SRCS_t07_explicit_data_signing  := t07_explicit_data_signing/vuln.c

TESTS    := t01_stack_bof_ret t02_ret2func t03_rop_2gadget t04_off_by_one_lr \
            t05a_fptr_overwrite t05b_data_only t06_sp_collision_pac_reuse \
            t07_explicit_data_signing
VARIANTS := nopac pac

# Per-test extra CFLAGS (per ADR-2026-05-30-007). Indexed by test name and
# variant: CFLAGS_<test>__<variant>. Tests that do not define an entry get
# an empty expansion in BUILD_RULE, so t01-t06 builds are unchanged.
#
# t07 uses a source-level toggle rather than a compiler-flag toggle: both
# variants build with the same COMMON_CFLAGS (plus pac-ret on the pac side
# for consistency with the suite), and the load-bearing difference is the
# -DUSE_PAC_DATA_SIGNING=1 below which gates the inline-asm pacia/autia
# helpers in vuln.c.
# -march=armv8.3-a is needed so the assembler accepts the `pacia` / `autia`
# mnemonics used by t07's inline asm. Empirically the pinned
# aarch64-unknown-linux-gnu-gcc toolchain in this project does NOT accept
# them at its default -march level, so this flag is required (see ADR-007
# implementation-notes "If the toolchain rejects the instruction mnemonic"
# clause).
CFLAGS_t07_explicit_data_signing__pac := -DUSE_PAC_DATA_SIGNING=1 -march=armv8.3-a

# Host-aware runner (matches baseline/aarch64-hello-pac/Makefile pattern).
# macOS: Docker linux/arm64 exposes real armv8.3 PAC via Apple Virtualization.
# Linux: assume native aarch64; binaries can run directly.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    DOCKER_RUN := docker run --rm --platform linux/arm64 \
                  -v $(CURDIR)/$(BUILD_DIR):/build alpine:3.20
else
    DOCKER_RUN :=
endif
export DOCKER_RUN

.PHONY: all build test test-% verify-static clean help

all: build

help:
	@echo "Targets:"
	@echo "  build          -- compile all 14 binaries (nopac + pac for 7 tests)"
	@echo "  test           -- build, then run the full harness"
	@echo "  test-<name>    -- run one test only (e.g. test-t01_stack_bof_ret)"
	@echo "  verify-static  -- objdump-grep paciasp/autiasp in every pac/ binary"
	@echo "  clean          -- remove build/"

$(BUILD_DIR)/nopac $(BUILD_DIR)/pac:
	mkdir -p $@

# Generate one rule per (variant, test) pair so make can build everything
# in parallel and pick up source-list changes automatically. We pass the
# CFLAGS variable name as a third argument to avoid shell-side case mapping.
define BUILD_RULE
$(BUILD_DIR)/$(1)/$(2): $(SRCS_$(2)) $(COMMON) common/win.h | $(BUILD_DIR)/$(1)
	$$(CC) $$($(3)) $(CFLAGS_$(2)__$(1)) -I common -o $$@ $(SRCS_$(2)) $(COMMON)
endef

$(foreach t,$(TESTS),$(eval $(call BUILD_RULE,nopac,$(t),NOPAC_CFLAGS)))
$(foreach t,$(TESTS),$(eval $(call BUILD_RULE,pac,$(t),PAC_CFLAGS)))

BINARIES := $(foreach v,$(VARIANTS),$(foreach t,$(TESTS),$(BUILD_DIR)/$(v)/$(t)))

build: $(BINARIES)

# Static verification: every pac/ binary must contain at least one paciasp
# and one autiasp instruction (mirrors verify-static in the PAC baseline).
verify-static: build
	@echo "[verify-static] checking paciasp/autiasp in pac/ binaries"
	@fail=0; for b in $(foreach t,$(TESTS),$(BUILD_DIR)/pac/$(t)); do \
	    pac_count=$$($(OBJDUMP) -d $$b | grep -cE "paciasp|pacia"); \
	    aut_count=$$($(OBJDUMP) -d $$b | grep -cE "autiasp|autia"); \
	    if [ "$$pac_count" -lt 1 ] || [ "$$aut_count" -lt 1 ]; then \
	        echo "FAIL: $$b paciasp/pacia=$$pac_count autiasp/autia=$$aut_count" >&2; \
	        fail=1; \
	    else \
	        echo "  OK   $$b (paciasp/pacia=$$pac_count, autiasp/autia=$$aut_count)"; \
	    fi; \
	done; \
	if [ "$$fail" -ne 0 ]; then exit 1; fi
	@echo "[verify-static] OK"

test: build
	python3 harness/run_tests.py --all

# Single-test invocation. The pattern stem $* is e.g. "t01_stack_bof_ret".
# Depends only on that test's nopac+pac binaries so a partial build works.
test-%: $(BUILD_DIR)/nopac/% $(BUILD_DIR)/pac/%
	python3 harness/run_tests.py --test $*

clean:
	rm -rf $(BUILD_DIR)
