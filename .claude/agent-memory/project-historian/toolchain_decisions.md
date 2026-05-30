---
name: toolchain-decisions
description: Settled toolchain and execution environment choices for all aarch64 baselines (ADRs 001-004)
metadata:
  type: project
---

As of ADR-2026-05-30-003/004 (last update 2026-05-30):

- **Target architecture**: aarch64 Linux ELF (ELF64 little-endian, static)
- **Cross-compiler**: `aarch64-unknown-linux-gnu-gcc` (Homebrew, `messense/macos-cross-toolchains` tap on macOS). Use plain `CC =`, not `CC ?=` — GNU Make's built-in `CC=cc` silently overrides `?=`.
- **Compiler flags (no-PAC)**: `-O0 -g -static -Wall -Wextra`
- **Compiler flags (PAC baseline)**: `-O0 -g -static -Wall -Wextra -mbranch-protection=pac-ret`
- **Linking**: static — no sysroot needed; binary self-contained
- **Execution (macOS Darwin)**: `docker run --rm --platform linux/arm64 -v $(CURDIR)/build:/build alpine:3.20 /build/<binary>` — uses Apple Virtualization framework, NOT QEMU emulation; provides real ARMv8.3 PAC hardware. `RUNNER` Makefile variable.
- **Execution (Linux)**: `qemu-aarch64 $(TARGET)` — QEMU user-mode, overridable via `RUNNER=`
- **`qemu-aarch64` does NOT exist on macOS** — Homebrew's `qemu` formula ships only `qemu-system-*` on Darwin; do not suggest installing it on macOS.

**Makefile target convention**: `all`, `compile`, `run`, `clean`; PAC baselines add `verify-static`, `verify-trap`, `verify`.

**PAC verification signals**: hardware PAC trap on Linux (including Apple Virtualization) delivers **SIGBUS (signal 7, exit 135)**, not SIGILL. Static binaries do not emit `.note.gnu.property`; use `objdump -d` grep for `paciasp`/`autiasp` as the authoritative static check.

**Rejected alternatives** (documented in ADRs, do not re-litigate without a new ADR):
- QEMU system-mode (too heavy for baseline)
- Dynamic linking (sysroot complexity)
- Clang cross-compilation (explicit sysroot required)
- Third-party macOS qemu-user ports (no reputable maintained option)
- Lima/UTM VM (lifecycle overhead)

**Why:** Docker `linux/arm64` on Apple Silicon is near-native speed, hardware-authentic for PAC, and scriptable from Make. QEMU user-mode would NOP PAC instructions.

**How to apply:** Any analysis tooling or future ADR that processes aarch64 binaries should assume this execution environment. PAC bypass analysis targets `baseline/aarch64-hello-pac/build/hello`. [[project-structure]] [[pac-baseline-status]]
