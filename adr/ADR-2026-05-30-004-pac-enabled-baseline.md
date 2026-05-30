# ADR-2026-05-30-004: PAC-Enabled aarch64 Hello-World Baseline

**Date**: 2026-05-30
**Status**: Implemented
**Implemented**: 2026-05-30
**Commit**: see `git log -- adr/ADR-2026-05-30-004-pac-enabled-baseline.md` (implementing commit message starts with "feat: implement ADR-2026-05-30-004")
**Author**: Security Architect

## Context

The baseline at `/Users/kejor/ee202c/project/baseline/aarch64-hello/` produces a working static aarch64 Linux ELF without any control-flow integrity hardening. For the CFI bypass surface analyzer (Project A, EE202C), we need a second baseline binary that opts in to ARMv8.3 **Pointer Authentication Codes (PAC)**. PAC is the principal armv8 control-flow hardening primitive on Apple Silicon and the most interesting target for our bypass surface analysis: function returns and indirect calls become signed with a hardware-derived MAC, and a mismatched signature traps.

We need:

1. A compiled artifact that contains PAC sign/authenticate instructions (`paciasp`/`autiasp`, `pacia`/`autia`, etc.) in the function prologues and epilogues — not just a flag in the binary's metadata.
2. A runtime environment that **actually executes** those instructions on PAC-capable hardware so the authentication is real, not silently NOP'd.
3. A way to verify both conditions from outside the binary (static markers) and from inside it (a deliberate corruption that the hardware must trap on).

Apple Silicon CPUs implement ARMv8.3-A PAC. The open question for this ADR is whether the **Linux kernel exposed inside `docker run --platform linux/arm64`** (which uses Apple's Virtualization framework) exposes PAC to userspace, and whether our static binary will use it.

PAC interacts with `-static` in a way that matters: PAC instructions encode as hints in the NOP space, so a PAC-signed binary still loads on a pre-armv8.3 CPU (the hints become NOPs and authentication is effectively disabled). This is important — we must verify PAC is being **enforced**, not just present in the instruction stream.

## Decision

Create a second baseline at `/Users/kejor/ee202c/project/baseline/aarch64-hello-pac/` that mirrors the existing hello-world but is compiled with `-mbranch-protection=pac-ret` and runs on the same Docker `linux/arm64` runner. Verification is a first-class build target.

Concrete choices:

| Concern | Choice |
|---|---|
| Source language | C (single file, `hello.c`; same content as the no-PAC baseline) |
| Compiler | `aarch64-unknown-linux-gnu-gcc` (same toolchain as ADR-002) |
| PAC flag | `-mbranch-protection=pac-ret` (return-address signing only; **not** `standard` for this ADR) |
| Linking | **Static** (`-static`, unchanged from ADR-001) |
| Optimization | `-O0 -g` (same as no-PAC baseline; keeps prologues inspectable) |
| GNU note | `-Wa,--generate-missing-build-notes=yes` not required; rely on `.note.gnu.property` emitted by gcc with `-mbranch-protection` |
| Runner | Same Darwin/Linux split as ADR-003 (Docker `linux/arm64` on macOS; `qemu-aarch64` on Linux **with caveats — see below**) |
| Directory | New sibling `baseline/aarch64-hello-pac/` (do **not** flag-variant the existing Makefile) |
| Verification | New `make verify` target that runs three independent checks: (a) `.note.gnu.property` contains `GNU_PROPERTY_AARCH64_FEATURE_1_PAC`, (b) `objdump -d` shows `paciasp`/`autiasp` in `main` prologue/epilogue, (c) a `make verify-trap` target builds a sibling `hello_corrupt` that deliberately clobbers the signed LR and confirms it traps with `SIGILL` (signal 4) on the runner |

**Rejected alternatives:**

- **Flag variant inside the existing `aarch64-hello` Makefile** (e.g., `make compile PAC=1`). Rejected because (i) the existing baseline is referenced by ADRs 001/002/003 as the *no-PAC* control sample for the analyzer, and a flag variant invites accidentally rebuilding it with PAC and contaminating the control; (ii) two independent directories make `file`/`objdump` diffs between the variants trivial; (iii) future hardening variants (BTI-only, PAC+BTI, MTE) will each want their own directory to keep ADR mapping 1:1.
- **`-mbranch-protection=standard` (PAC-ret + BTI)** for the first PAC ADR. Rejected for this iteration because BTI requires every indirect branch target to begin with a `bti` landing pad and interacts with the C runtime startup objects in the static glibc we link against. If `crt1.o`/`libc.a` in the `messense/macos-cross-toolchains` build was not itself compiled with BTI, mixing a BTI-marked `main` with non-BTI crt code can produce subtle behavior. PAC-ret has no such whole-program requirement — it is purely a per-function prologue/epilogue change — and is the minimum useful PAC surface for our analyzer. A follow-up ADR can add BTI once PAC-ret is verified working.
- **`-mbranch-protection=pac-ret+leaf`** (sign even leaf functions). Rejected: not interesting for `main` (which is a leaf in this hello-world) only because we want the *same* compilation profile we will later apply to non-trivial binaries. `pac-ret` skips leaves by default, which is the production default; using `+leaf` here would over-fit the baseline.
- **Run under `qemu-aarch64` user-mode emulation as the macOS path.** Already rejected by ADR-003 for portability reasons; additionally relevant here because **`qemu-aarch64` user-mode emulation does not faithfully enforce PAC authentication on hosts whose kernel does not advertise PAC to qemu** — PAC instructions are translated as hints/NOPs and authentication failures will not trap. Apple Virtualization framework on Apple Silicon, by contrast, gives us a real armv8.3 CPU under the container.
- **Static `-static` with PAC is fine.** PAC instructions are emitted into the user code regardless of linking mode. The constraint with `-static` is that **glibc's own `.a` archives may not be compiled with PAC**, so PAC will be active only in our code, not throughout the call stack. This is acceptable for the analyzer's purposes (we are analyzing the bypass surface of *our* code, not glibc). Documented in Implementation Notes.

## Rationale

- **`-mbranch-protection=pac-ret` is the smallest useful PAC opt-in.** It adds exactly two new instructions per non-leaf function (`paciasp` in the prologue, `autiasp` before `ret`). This is the cleanest possible diff against the no-PAC baseline and the easiest to reason about when analyzing CFI bypass surface.
- **Separate directory keeps the no-PAC control immutable.** ADRs 001/002/003 treat `baseline/aarch64-hello/` as a reference artifact. We do not want PAC to leak into it via a stale env var or Makefile variable.
- **Reusing the Docker `linux/arm64` runner is correct on Apple Silicon.** Apple's Virtualization framework presents the underlying M-series CPU's armv8.3 ISA to the guest. The Linux kernel inside `alpine:3.20` reports PAC support in `/proc/cpuinfo` (`Features: ... paca pacg`) and via `HWCAP_PACA`/`HWCAP_PACG` to userspace on PAC-capable hosts. Our static binary will therefore execute real PAC instructions and the kernel will deliver `SIGILL` on an authentication failure.
- **Verification must include a runtime trap test.** Static markers (`.note.gnu.property`, disassembly) prove the binary *would* use PAC; they do not prove the *runner* enforces it. A deliberate-corruption variant that we build, run, and assert traps on closes that gap. This is the only check that distinguishes "PAC compiled in" from "PAC active."
- **PAC-ret without BTI is a real, deployable configuration.** Android's userspace and many distros ship `pac-ret` without `bti` on systems where library BTI coverage is incomplete. Modeling that profile in the baseline matches the threat surface the analyzer will encounter in the wild.

## Implementation Notes

**Directory layout:**

```
baseline/
  aarch64-hello/         (unchanged; ADRs 001-003)
  aarch64-hello-pac/
    hello.c              (identical to aarch64-hello/hello.c)
    hello_corrupt.c      (variant that clobbers the signed return address)
    Makefile
    README.md
```

**`hello.c`** — copy verbatim from `baseline/aarch64-hello/hello.c`. The whole point is that the source is identical and only the build flags differ.

**`hello_corrupt.c`** — a minimal program that:

1. Has a non-leaf function `victim()` (e.g., calls `puts` so the compiler does not inline) which the compiler will sign with `paciasp`/`autiasp` under `-mbranch-protection=pac-ret`.
2. Inside `victim`, uses inline asm (`mov x30, #0xdead`) or a controlled stack write to corrupt the saved LR after it has been signed but before `autiasp` runs.
3. Returns from `victim`. On a PAC-enforcing runtime, `autiasp` will fail authentication and the subsequent `ret` will trap with `SIGILL`. On a non-enforcing runtime, the corrupted LR will be used and the program will either segfault elsewhere or jump into the weeds — distinguishably different from a clean PAC trap.

A reference implementation:

```c
#include <stdio.h>

__attribute__((noinline))
static void victim(void) {
    puts("entered victim");
    /* Clobber the saved LR on the stack. With -O0 the LR is spilled to
       [sp, #8] (or similar) after paciasp. The exact offset is verified
       by `objdump -d` during bring-up and pinned in the comment below. */
    asm volatile (
        "mov x9, #0xdead\n\t"
        "str x9, [sp, #8]\n\t"
        : : : "x9", "memory"
    );
    puts("about to return from victim");
}

int main(void) {
    victim();
    puts("returned from victim (PAC NOT enforced)");
    return 0;
}
```

The `str x9, [sp, #8]` offset is compiler/version-dependent. During bring-up, disassemble `victim` and confirm the offset matches the slot used for the saved LR; record the verified offset in the file's leading comment. A PAC-enforcing runtime will never print `returned from victim (PAC NOT enforced)`.

**`Makefile`** — start from the no-PAC baseline and add PAC flags plus verification targets. Use the same `RUNNER` abstraction from ADR-003:

```makefile
# PAC-enabled aarch64 hello-world build (ADR-2026-05-30-004).
# Builds on ADRs 001-003 (toolchain, target naming, Darwin/Linux runner split).

CC      ?= aarch64-unknown-linux-gnu-gcc
PAC_FLAGS := -mbranch-protection=pac-ret
CFLAGS  := -O0 -g -static -Wall -Wextra $(PAC_FLAGS)

BUILD_DIR := build
TARGET    := $(BUILD_DIR)/hello
CORRUPT   := $(BUILD_DIR)/hello_corrupt
SRC       := hello.c
SRC_C     := hello_corrupt.c

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

# Static verification: PAC marker in GNU notes + paciasp/autiasp in disassembly.
verify-static: compile
	@echo "[verify-static] checking .note.gnu.property for PAC bit"
	@aarch64-unknown-linux-gnu-readelf -n $(TARGET) | grep -E "AArch64 feature: .*PAC" \
	    || (echo "FAIL: PAC bit not present in .note.gnu.property" >&2; exit 1)
	@echo "[verify-static] checking for paciasp/autiasp in main"
	@aarch64-unknown-linux-gnu-objdump -d $(TARGET) | grep -E "paciasp|autiasp" \
	    || (echo "FAIL: no paciasp/autiasp instructions in disassembly" >&2; exit 1)
	@echo "[verify-static] OK"

# Runtime trap verification: corrupt LR after signing; expect SIGILL (exit 132 = 128+4).
verify-trap: compile
	@echo "[verify-trap] running hello_corrupt; expecting SIGILL (exit 132)"
	@$(RUNNER_CORRUPT); status=$$?; \
	if [ $$status -eq 132 ] || [ $$status -eq 139 ]; then \
	    echo "[verify-trap] OK (runner exited $$status: PAC authentication failed as expected)"; \
	elif [ $$status -eq 0 ]; then \
	    echo "FAIL: hello_corrupt exited 0; PAC is NOT enforced by the runner" >&2; exit 1; \
	else \
	    echo "WARN: hello_corrupt exited $$status (not 132/139); inspect manually" >&2; exit 1; \
	fi

verify: verify-static verify-trap

clean:
	rm -rf $(BUILD_DIR)
```

Notes on the verification targets:

- **`verify-static`** uses `readelf -n` to inspect `.note.gnu.property`. With `-mbranch-protection=pac-ret`, gcc emits the `GNU_PROPERTY_AARCH64_FEATURE_1_AND` property with the `PAC` bit set. The grep matches `readelf`'s human-readable line (e.g., `AArch64 feature: PAC`).
- **`verify-trap`** accepts exit 132 (`SIGILL`) or 139 (`SIGSEGV`) as success. The expected signal under Linux PAC enforcement is `SIGILL` on `aut*` failure (the kernel injects `SIGILL` with `si_code = ILL_BTCFI` / PAC-specific). Some kernels deliver `SIGSEGV` instead depending on PAC failure handling configuration; both indicate the corruption was caught. Exit 0 is the only outcome that means PAC is not enforced and is a hard failure.

**Verification ordering during bring-up:**

1. `make compile` — confirms the compiler accepts `-mbranch-protection=pac-ret`.
2. `make verify-static` — confirms PAC instructions are in the binary.
3. `make run` — confirms the unmodified PAC-signed binary still runs successfully on the runner (PAC instructions don't crash a clean program).
4. `make verify-trap` — confirms the runner enforces PAC. This is the decisive test.

If step 4 fails on macOS (exit 0), it means Docker on this host did not expose PAC to the container. Document the macOS, Docker Desktop, and Virtualization framework versions in the README's bring-up log so we can correlate with future regressions.

**Gotchas:**

- **PAC is a hint-space encoding.** A PAC-signed binary runs fine on pre-armv8.3 hardware with PAC instructions silently becoming NOPs. This is why `verify-trap` is mandatory — `verify-static` alone is not enough.
- **glibc in the static link is not PAC-signed.** The `messense/macos-cross-toolchains` build of glibc was not (at time of writing) compiled with `-mbranch-protection`. This means only *our* code's returns are signed. For the analyzer's purposes this is fine and is actually closer to the real-world situation we'll analyze; document it explicitly.
- **`-O0` keeps `paciasp`/`autiasp` visible.** Higher optimization can inline small functions away. Do not change `-O0` for the baseline.
- **`hello_corrupt.c` is fragile by design.** The exact stack offset of the signed LR depends on compiler version. The Makefile must not fail silently if the corruption misses the LR slot; that is what `verify-trap`'s "WARN" branch is for. Inspect `objdump` output on first bring-up and pin the offset in a source comment.
- **`docker run` exit code.** Docker propagates the container's exit code, including signal-derived codes (128 + signum). This is what `verify-trap` relies on.
- **Do not edit the no-PAC baseline.** Specifically, do not let `CFLAGS` propagate from environment into `baseline/aarch64-hello/`. The Makefiles are sibling and independent.
- **`readelf` / `objdump` binaries.** Use the cross-toolchain's `aarch64-unknown-linux-gnu-readelf` and `aarch64-unknown-linux-gnu-objdump`, not host `readelf`. Host `readelf` on macOS may not exist or may not parse `.note.gnu.property` AArch64 feature bits.

**Security considerations:**

- **PAC is hardware-rooted.** Authentication uses CPU-internal keys (APIAKey, etc.) that userspace cannot read. Our verification trap test demonstrates that the keys are active and that the kernel did not silently configure PAC to log-only.
- **Apple Silicon PAC keys.** macOS uses PAC pervasively in its own kernel; the Virtualization framework gives the Linux guest its own PAC key context. Cross-VM PAC forgery is not a concern for this baseline.
- **Threat model unchanged.** This baseline still runs only our trusted code. The deliberate-corruption variant is self-targeting; it does not execute untrusted input.
- **PAC bypass is the analyzer's eventual goal.** This ADR establishes the *target* surface, not a defended runtime. We expect later ADRs (analyzer ADRs) to enumerate the bypass primitives against this exact binary.

## Consequences

**Enables:**

- A second baseline that is identical-source / different-flags to the no-PAC control, suitable for diff-based analysis (compare disassembly, compare CFI bypass surface metrics).
- A reusable `verify-static` / `verify-trap` pattern that future hardening baselines (BTI, PAC+BTI, MTE) can copy.
- An end-to-end demonstration that Apple Silicon + Docker `linux/arm64` is a viable PAC execution environment for the project.

**Constraints:**

- Locks the project's PAC variant to **`pac-ret` only** for now. Adding BTI is a separate ADR.
- The trap test depends on the exact stack-spill layout of `victim()` under `-O0`; toolchain upgrades may require re-pinning the offset.
- Adds two new make targets (`verify-static`, `verify-trap`) to the convention; future baselines should implement at least the static check.
- If the developer's Docker Desktop or macOS version stops exposing PAC to the guest, `verify-trap` will start failing. This is the desired behavior — silent loss of PAC enforcement would be worse — but it means the baseline's "passing" state depends on the host stack, not just the source.

## Acceptance Criteria

- [ ] `/Users/kejor/ee202c/project/baseline/aarch64-hello-pac/` exists with `hello.c`, `hello_corrupt.c`, `Makefile`, and `README.md`.
- [ ] `baseline/aarch64-hello-pac/hello.c` is byte-identical to `baseline/aarch64-hello/hello.c`.
- [ ] `cd baseline/aarch64-hello-pac && make compile` produces `build/hello` and `build/hello_corrupt`, both statically linked aarch64 ELFs (`file` confirms).
- [ ] `make verify-static` passes: `.note.gnu.property` contains the AArch64 PAC feature bit, and `objdump -d build/hello` shows `paciasp` and `autiasp` in `main` (or whichever non-leaf function the compiler picks).
- [ ] `make run` prints `Hello, aarch64` and exits 0 on the macOS Docker runner.
- [ ] `make verify-trap` exits 0 (test passes) with the runner having exited 132 or 139, demonstrating PAC enforcement at runtime.
- [ ] `make verify` runs both `verify-static` and `verify-trap` in sequence and passes.
- [ ] `make clean` removes `build/`.
- [ ] README documents: (a) the `-mbranch-protection=pac-ret` flag and why not `standard`, (b) the requirement that the runner expose PAC, (c) the verified `objdump` offset for the LR clobber in `hello_corrupt.c`, (d) the host environment (macOS version, Docker Desktop version, `uname -a` inside the container) captured during initial bring-up.
- [ ] `baseline/aarch64-hello/` is unmodified by this change.
- [ ] `.gitignore` continues to ignore `baseline/*/build/`; no built artifacts committed.
