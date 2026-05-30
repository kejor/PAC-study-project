# ADR: ADR-2026-05-30-002 — macOS aarch64 Toolchain and Makefile Target Naming Fix

**Timestamp**: 2026-05-30 10:15 (UTC-5)
**Type**: ADR
**Author/Source**: Agent (security-architect)
**Tags**: adr, adr-implemented, toolchain, makefile, homebrew, aarch64, cross-compiler
**Commit**: `8629cdf`

## Summary

ADR-2026-05-30-002 was drafted and implemented to resolve two build-blocking defects in `baseline/aarch64-hello/` on macOS: the wrong cross-compiler formula name and a Makefile circular dependency. The fix adopts the `messense/macos-cross-toolchains` Homebrew tap (formula: `aarch64-unknown-linux-gnu`) and renames the phony `build` target to `compile`.

## Details

**Files modified:**
- `baseline/aarch64-hello/Makefile` — three changes:
  - Line 4: `CC ?= aarch64-linux-gnu-gcc` → `CC ?= aarch64-unknown-linux-gnu-gcc`
  - `.PHONY` declaration: `build` → `compile`
  - Phony target and its dependent rules renamed from `build` to `compile`
- `baseline/aarch64-hello/README.md` — updated install command to include `brew tap messense/macos-cross-toolchains` and `brew install aarch64-unknown-linux-gnu`

**ADR file:** `adr/ADR-2026-05-30-002-macos-toolchain-and-makefile-fix.md`

## Rationale / Decision Basis

- `messense/macos-cross-toolchains` is the de facto solution for aarch64-linux cross-compilation on macOS; it ships precompiled binaries for both Intel and Apple Silicon and produces ELFs that run under QEMU (and Docker `linux/arm64`) without additional sysroot configuration.
- Renaming the phony target (not the directory) is the minimal change: all artifact paths documented in ADR-001 remain intact. `build/` stays `build/`, `build/hello` stays `build/hello`.
- The `CC ?=` form was preserved (at this stage) to allow `CC=aarch64-linux-gnu-gcc make` overrides from Linux CI; this was later found to be insufficient and corrected in commit `3468643` (see bug fix record for GNU Make `CC` default shadowing).

**Alternatives rejected:** Docker-based cross-compilation (over-engineered for a hello-world baseline); LLVM/clang with a musl/glibc sysroot (reintroduces the sysroot complexity ADR-001 explicitly rejected); renaming `build/` to `out/` (churns directory layout already in ADRs).

## Impact

- Unblocked `make compile` and `make run` on macOS Apple Silicon.
- Establishes `compile` / `run` / `clean` as the canonical target vocabulary for all future `baseline/*/Makefile`s.
- Adds `messense/macos-cross-toolchains` as a documented third-party Homebrew tap dependency; supply-chain risk is noted in the ADR.
- The toolchain triple changes from `aarch64-linux-gnu` (ADR-001 documentation) to `aarch64-unknown-linux-gnu` (macOS Homebrew convention); future ADRs should use the latter for macOS toolchain references.

## Related Records

- `records/2026-05-30_10-00_bug-fix_aarch64-hello-toolchain-and-circular-dep.md`
- `records/2026-05-30_11-00_bug-fix_qemu-aarch64-missing-on-macos.md`
- `adr/ADR-2026-05-25-001-aarch64-qemu-hello-world.md`
- `adr/ADR-2026-05-30-002-macos-toolchain-and-makefile-fix.md`
