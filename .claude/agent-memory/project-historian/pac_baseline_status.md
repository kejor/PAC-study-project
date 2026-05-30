---
name: pac-baseline-status
description: Current status of the PAC-enabled aarch64 baseline and what it proves about the execution environment
metadata:
  type: project
---

As of 2026-05-30, `baseline/aarch64-hello-pac/` is fully passing (`make verify` + `make run` green).

**What is confirmed working:**
- `aarch64-unknown-linux-gnu-gcc -mbranch-protection=pac-ret` emits `paciasp`/`autiasp` in function prologues/epilogues
- Docker `linux/arm64` on Apple Silicon (Apple Virtualization) enforces PAC authentication at runtime
- Deliberate LR clobber in `hello_corrupt` → SIGBUS (exit 135) on the runner — hardware is real

**PAC trap signal on this stack:** SIGBUS (signal 7, exit 135 = 128+7). Not SIGILL (132), not SIGSEGV (139). All three are accepted in `verify-trap` for robustness.

**Static verification:** `objdump -d build/hello | grep -E "paciasp|autiasp"` is the authoritative check. `.note.gnu.property` is absent in static binaries and cannot be used.

**GNU Make CC gotcha (fixed in commit 3468643):** `CC ?= ...` is overridden by GNU Make's built-in `CC=cc`. All cross-toolchain Makefiles in this project use plain `CC =`.

**Why this matters:** The Apple Virtualization path is hardware-authentic — PAC keys are active, `autiasp` enforces authentication. QEMU user-mode would NOP PAC instructions. This is the correct execution environment for CFI bypass surface analysis.

**How to apply:** When writing analyzer ADRs or analysis scripts, assume the runner exposes real PAC hardware. `baseline/aarch64-hello-pac/build/hello` is the target binary. [[toolchain-decisions]]
