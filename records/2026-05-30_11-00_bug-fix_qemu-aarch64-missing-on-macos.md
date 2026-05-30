# Bug Fix: `qemu-aarch64` Missing on macOS — Homebrew QEMU Formula Limitation

**Timestamp**: 2026-05-30 11:00 (UTC-5)
**Type**: Bug Fix
**Author/Source**: User / Agent
**Tags**: bug-fix, qemu, macos, homebrew, execution-environment, aarch64

## Summary

After ADR-2026-05-30-002 resolved the cross-compiler and Makefile defects, `make run` still failed on macOS with `qemu-aarch64: command not found`. Homebrew's `qemu` formula (v11.0.1) ships only `qemu-system-*` binaries on macOS; user-mode QEMU emulators (`qemu-aarch64`, `qemu-arm`, etc.) are not packaged for Darwin because they depend on Linux-specific kernel primitives (`binfmt_misc`, Linux syscall ABI) that do not exist on macOS.

## Details

**Error observed:**
```
/bin/sh: qemu-aarch64: command not found
qemu-aarch64 exited with status 127
```

**Root cause:** QEMU user-mode translates Linux syscalls by forwarding them to the host kernel via the Linux syscall ABI. This mechanism requires a Linux host kernel; it is architecturally incompatible with Darwin. Homebrew does not build or package `qemu-user` for macOS, and no reputable maintained port exists as a Homebrew formula.

**Binaries present in `/opt/homebrew/bin/` from Homebrew QEMU on the developer's host:**
`qemu-system-aarch64`, `qemu-img`, `qemu-io`, `qemu-nbd`, `qemu-storage-daemon`, `qemu-edid` — none of which are user-mode execution shims for aarch64 ELFs.

The cross-compiled artifact at `baseline/aarch64-hello/build/hello` was confirmed valid via `file` (static aarch64 Linux ELF); the problem was purely the execution environment gap on macOS.

## Impact

This was the second blocking issue preventing baseline validation on macOS. ADR-001's assumption that `qemu-aarch64` would be available via `brew install qemu` was incorrect for macOS. The resolution required a new ADR (ADR-2026-05-30-003) replacing the QEMU-based runner with Docker `linux/arm64` on macOS.

## Related Records

- `records/2026-05-30_11-15_adr_macos-qemu-user-mode-workaround.md`
- `records/2026-05-30_10-15_adr_macos-toolchain-and-makefile-fix.md`
- `adr/ADR-2026-05-30-003-macos-qemu-user-mode-workaround.md`
