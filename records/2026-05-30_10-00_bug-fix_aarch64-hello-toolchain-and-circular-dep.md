# Bug Fix: aarch64-hello Baseline Run Failure — Toolchain and Circular Dependency

**Timestamp**: 2026-05-30 10:00 (UTC-5)
**Type**: Bug Fix
**Author/Source**: User / Agent
**Tags**: bug-fix, makefile, toolchain, aarch64, homebrew, cross-compiler

## Summary

`make run` on `baseline/aarch64-hello/` failed with two independent defects surfacing simultaneously: the assumed Homebrew cross-compiler formula does not exist, and the Makefile had a circular dependency that silently dropped the build rule. These are distinct root causes that both required fixes before the baseline could run end-to-end.

## Details

**Defect 1 — Missing cross-compiler formula.**
`CC ?= aarch64-linux-gnu-gcc` in the Makefile assumes a Homebrew formula named `aarch64-linux-gnu`. No such formula exists in Homebrew core or any well-maintained tap. Make fell through to the system default `cc`, which on macOS resolves to Apple Clang. Clang then failed linking because it could not locate `crt0.o` — it was targeting `darwin`, not `aarch64-linux`, and had no Linux sysroot available.

**Defect 2 — Makefile circular dependency.**
The phony target was named `build` and the build output directory was also `build/`. With `TARGET := build/hello` and an order-only prerequisite on `$(BUILD_DIR)`, GNU Make resolved the `build/` path component against the `build` phony target and emitted:
```
Circular build/hello <- build dependency dropped
```
This silently dropped the compile rule, so the binary was never produced.

## Impact

Both defects were blocking: the no-PAC baseline could not be built or executed on the developer's macOS Apple Silicon host. This was the gating issue for all subsequent baseline work. Resolution tracked in ADR-2026-05-30-002.

## Related Records

- `records/2026-05-30_10-15_adr_macos-toolchain-and-makefile-fix.md`
- `adr/ADR-2026-05-30-002-macos-toolchain-and-makefile-fix.md`
