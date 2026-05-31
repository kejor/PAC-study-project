# PAC vulnerability test suite (ADR-2026-05-30-005, expanded by -006, -007,
# -008, -009). Builds nine self-contained vulnerability programs; t01-t08
# each in two variants (nopac, pac); t09 in three variants
# (nopac, pac, pac_leaf -- per ADR-2026-05-30-009) and drives a Python
# harness that checks expected outcomes.

CC      = aarch64-unknown-linux-gnu-gcc
OBJDUMP = aarch64-unknown-linux-gnu-objdump

# -fno-stack-protector is MANDATORY: with the canary on, __stack_chk_fail
# would catch t01/t02/t04 overflows before PAC has a chance to, masking
# what we're trying to measure. See ADR Implementation Notes.
# -z execstack is included only to document that DEP is not the gating
# mitigation; none of the tests actually execute stack data.
COMMON_CFLAGS := -O0 -g -static -no-pie -Wall -Wextra \
                 -fno-stack-protector -Wl,-z,execstack
NOPAC_CFLAGS    := $(COMMON_CFLAGS)
PAC_CFLAGS      := $(COMMON_CFLAGS) -mbranch-protection=pac-ret
# pac_leaf: LLVM/GCC SignReturnAddress::All -- signs leaf functions too.
# Closes the leaf-signing gap that t09 demonstrates (ADR-2026-05-30-009).
PAC_LEAF_CFLAGS := $(COMMON_CFLAGS) -mbranch-protection=pac-ret+leaf

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
SRCS_t08_pac_oracle_bruteforce  := t08_pac_oracle_bruteforce/vuln.c
SRCS_t09_leaf_signing_gap       := t09_leaf_signing_gap/vuln.c

TESTS    := t01_stack_bof_ret t02_ret2func t03_rop_2gadget t04_off_by_one_lr \
            t05a_fptr_overwrite t05b_data_only t06_sp_collision_pac_reuse \
            t07_explicit_data_signing t08_pac_oracle_bruteforce \
            t09_leaf_signing_gap

# Per-test variant lists (ADR-2026-05-30-009). Tests without an explicit
# VARIANTS_<test> entry inherit VARIANTS_default. t09 is the only test
# that currently uses pac_leaf; t01-t08 would build identical binaries
# under pac_leaf as under pac for everything except leaf functions they
# do not exercise, so building them would just add cost without signal.
VARIANTS_default            := nopac pac
VARIANTS_t09_leaf_signing_gap := nopac pac pac_leaf

# VARIANTS is the UNION of every per-test variant list. It is used only
# for the directory-creation rule below (mkdir build/<v>) -- the actual
# per-binary build foreach uses VARIANTS_<test> via ADD_TEST_BINARIES.
VARIANTS := nopac pac pac_leaf

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

# t08 (per ADR-2026-05-30-008): software PAC oracle / brute-force test. Both
# variants receive -DBRUTE_BITS=8 so the source can uniformly reference the
# constant; only the pac variant defines USE_PAC_BRUTE=1 to enable the
# inline-asm pacia / blraa helpers. -march=armv8.3-a is required (same as
# t07) so the assembler accepts those mnemonics.
CFLAGS_t08_pac_oracle_bruteforce__pac   := -DUSE_PAC_BRUTE=1 -DBRUTE_BITS=8 -march=armv8.3-a
CFLAGS_t08_pac_oracle_bruteforce__nopac := -DBRUTE_BITS=8

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
	@echo "  build          -- compile all 19 binaries (nopac+pac for t01-t08, nopac+pac+pac_leaf for t09)"
	@echo "  test           -- build, then run the full harness"
	@echo "  test-<name>    -- run one test only (e.g. test-t01_stack_bof_ret)"
	@echo "  verify-static  -- objdump-grep paciasp/autiasp in every pac/ and pac_leaf/ binary"
	@echo "  clean          -- remove build/"

$(BUILD_DIR)/nopac $(BUILD_DIR)/pac $(BUILD_DIR)/pac_leaf:
	mkdir -p $@

# Generate one rule per (variant, test) pair so make can build everything
# in parallel and pick up source-list changes automatically. We pass the
# CFLAGS variable name as a third argument to avoid shell-side case mapping.
define BUILD_RULE
$(BUILD_DIR)/$(1)/$(2): $(SRCS_$(2)) $(COMMON) common/win.h | $(BUILD_DIR)/$(1)
	$$(CC) $$($(3)) $(CFLAGS_$(2)__$(1)) -I common -o $$@ $(SRCS_$(2)) $(COMMON)
endef

# Register build rules for every (variant, test) pair. We still define a
# rule for every variant on every test (cheap -- just macro expansion); the
# per-test variant restriction is enforced by which binaries we actually
# request via BINARIES below.
$(foreach t,$(TESTS),$(eval $(call BUILD_RULE,nopac,$(t),NOPAC_CFLAGS)))
$(foreach t,$(TESTS),$(eval $(call BUILD_RULE,pac,$(t),PAC_CFLAGS)))
$(foreach t,$(TESTS),$(eval $(call BUILD_RULE,pac_leaf,$(t),PAC_LEAF_CFLAGS)))

# BINARIES is the per-test-restricted list: for each test, take its
# VARIANTS_<test> if set, else VARIANTS_default. Yields 8 tests x 2 + 1
# test x 3 = 19 binaries with the current configuration.
BINARIES :=
define ADD_TEST_BINARIES
$(foreach v,$(or $(VARIANTS_$(1)),$(VARIANTS_default)),$(eval BINARIES += $(BUILD_DIR)/$(v)/$(1)))
endef
$(foreach t,$(TESTS),$(eval $(call ADD_TEST_BINARIES,$(t))))

build: $(BINARIES)

# Static verification: every pac/ and pac_leaf/ binary must contain at
# least one paciasp and one autiasp instruction (mirrors verify-static in
# the PAC baseline). Plus a t09-specific assertion (ADR-2026-05-30-009):
# pac/t09_leaf_signing_gap's leaf_victim must contain ZERO paciasp (that
# IS the gap), and pac_leaf/t09_leaf_signing_gap's leaf_victim must
# contain at least one paciasp (that is the mitigation).
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
	@echo "[verify-static] checking paciasp/autiasp in pac_leaf/ binaries"
	@fail=0; for b in $(BUILD_DIR)/pac_leaf/t09_leaf_signing_gap; do \
	    [ -e "$$b" ] || continue; \
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
	@echo "[verify-static] checking t09 leaf-signing-gap invariant (ADR-009)"
	@leaf_pac_count=$$($(OBJDUMP) -d $(BUILD_DIR)/pac/t09_leaf_signing_gap \
	    | awk '/<leaf_victim>:/,/^$$/' | grep -cE "paciasp"); \
	if [ "$$leaf_pac_count" -ne 0 ]; then \
	    echo "FAIL: pac/t09 leaf_victim contains $$leaf_pac_count paciasp; expected 0 (the gap)" >&2; \
	    exit 1; \
	fi; \
	echo "  OK   pac/t09 leaf_victim has 0 paciasp (gap confirmed)"
	@leaf_pac_count_l=$$($(OBJDUMP) -d $(BUILD_DIR)/pac_leaf/t09_leaf_signing_gap \
	    | awk '/<leaf_victim>:/,/^$$/' | grep -cE "paciasp"); \
	if [ "$$leaf_pac_count_l" -lt 1 ]; then \
	    echo "FAIL: pac_leaf/t09 leaf_victim missing paciasp (mitigation broken)" >&2; \
	    exit 1; \
	fi; \
	echo "  OK   pac_leaf/t09 leaf_victim has $$leaf_pac_count_l paciasp (mitigation confirmed)"
	@echo "[verify-static] OK"

test: build
	python3 harness/run_tests.py --all

# Single-test invocation. The pattern stem $* is e.g. "t01_stack_bof_ret".
# Depends only on that test's nopac+pac binaries so a partial build works.
test-%: $(BUILD_DIR)/nopac/% $(BUILD_DIR)/pac/%
	python3 harness/run_tests.py --test $*

# t09 also requires the pac_leaf variant (ADR-2026-05-30-009). The
# pattern rule above covers nopac+pac; this explicit rule layers the
# pac_leaf prereq on top for t09 only, without changing the pattern
# rule's command (the harness picks up variants from expectations.yml).
test-t09_leaf_signing_gap: $(BUILD_DIR)/pac_leaf/t09_leaf_signing_gap

clean:
	rm -rf $(BUILD_DIR)
