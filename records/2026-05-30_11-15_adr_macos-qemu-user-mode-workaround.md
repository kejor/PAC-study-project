# ADR: ADR-2026-05-30-003 — macOS QEMU User-Mode Workaround via Docker linux/arm64

**Timestamp**: 2026-05-30 11:15 (UTC-5)
**Type**: ADR
**Author/Source**: Agent (security-architect)
**Tags**: adr, adr-implemented, docker, qemu, macos, apple-silicon, apple-virtualization, aarch64, runner
**Commit**: `189ac27`

## Summary

ADR-2026-05-30-003 was drafted and implemented to resolve the absence of `qemu-aarch64` on macOS. The fix replaces the `QEMU` Makefile variable with a platform-aware `RUNNER` abstraction: on Darwin, the runner is `docker run --rm --platform linux/arm64 -v $(CURDIR)/build:/build alpine:3.20 /build/hello`; on Linux, it remains `qemu-aarch64 $(TARGET)`. This is not emulation on Apple Silicon — Docker uses Apple's Virtualization framework, providing a native armv8.3 execution environment.

## Details

**Files modified:**
- `baseline/aarch64-hello/Makefile`:
  - Removed the `QEMU` variable
  - Added `UNAME_S := $(shell uname -s)` OS detection
  - Added `ifeq ($(UNAME_S),Darwin)` / `else` block defining `RUNNER`
  - Darwin `RUNNER`: `docker run --rm --platform linux/arm64 -v $(CURDIR)/$(BUILD_DIR):/build alpine:3.20 /build/hello`
  - Linux `RUNNER`: `qemu-aarch64 $(TARGET)`
  - `run` target updated to invoke `$(RUNNER)` (no appended `$(TARGET)` on Darwin path — the binary path is embedded in the runner command)
- `baseline/aarch64-hello/README.md` — added Docker Desktop as macOS prerequisite, documented `--platform linux/arm64` mechanism, pinned `alpine:3.20`, documented `RUNNER=` override

**ADR file:** `adr/ADR-2026-05-30-003-macos-qemu-user-mode-workaround.md`

## Rationale / Decision Basis

**Key architectural insight:** Docker Desktop on Apple Silicon runs `linux/arm64` containers via the Apple Virtualization framework, not software emulation. The underlying M-series CPU's ARMv8.3 ISA — including PAC hardware — is directly exposed to the container guest. This is near-native execution speed. There is no QEMU translation layer for aarch64 on macOS with this path.

A static binary requires no library resolution inside the container; `alpine:3.20` is used only as the Linux kernel/userspace boundary. The binary brings its own libc.

**Alternatives rejected:** `qemu-system-aarch64` + minimal Linux VM (over-engineered for a hello-world; hundreds of MB of additional artifacts); Lima/UTM VM (lifecycle management overhead, not suitable for the tight inner loop needed by the CFI analyzer); CI-only execution (not viable for a project requiring hundreds of instrumented-binary runs during development); third-party macOS qemu-user ports (no reputable maintained option exists).

## Impact

- Unblocked `make run` end-to-end on macOS Apple Silicon; `Hello, aarch64` printed with exit 0.
- Established `RUNNER` as the canonical abstraction for all `baseline/*/Makefile`s; extensible to other architectures (e.g., `linux/amd64`, `linux/riscv64`).
- **Critical for the PAC work that followed:** because Docker uses Apple Virtualization, the container has access to real ARMv8.3 PAC hardware, not a QEMU NOP-based emulation. This is what makes `make verify-trap` in ADR-2026-05-30-004 meaningful.
- Adds Docker Desktop as a documented macOS dependency (heavyweight but standard on developer machines).
- The `QEMU` variable name from ADR-001 is superseded; references in future ADRs should use `RUNNER`.

## Related Records

- `records/2026-05-30_11-00_bug-fix_qemu-aarch64-missing-on-macos.md`
- `records/2026-05-30_12-00_milestone_no-pac-baseline-passing.md`
- `adr/ADR-2026-05-30-003-macos-qemu-user-mode-workaround.md`
