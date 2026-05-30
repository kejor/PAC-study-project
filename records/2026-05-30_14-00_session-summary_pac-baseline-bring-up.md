# Session Summary: PAC Baseline Bring-Up and Dual-Baseline Operational

**Timestamp**: 2026-05-30 14:00 (UTC-5)
**Type**: Session Summary
**Author/Source**: Agent (security-architect + adr-implementer agents)
**Tags**: session-summary, pac, aarch64, baseline, docker, apple-silicon, verify-trap, gnu-make, toolchain

## Summary

This session completed the no-PAC baseline bring-up (blocked since 2026-05-25 by two independent macOS defects) and then established a new PAC-enabled baseline in `baseline/aarch64-hello-pac/`. Four ADRs were implemented across five commits. By end of session both baselines pass all verification targets, and a hardware-authentic PAC execution environment on macOS Apple Silicon is confirmed operational.

## Details

### Arc of the session

**Phase 1 — Unblocking the no-PAC baseline (two blocking defects)**

1. `make run` on `baseline/aarch64-hello/` failed with a missing cross-compiler: `CC ?= aarch64-linux-gnu-gcc` resolved to Apple Clang (no Linux `crt0.o`). ADR-2026-05-30-002 fixed this by adopting the `messense/macos-cross-toolchains` tap (`aarch64-unknown-linux-gnu`) and renaming the phony target `build` → `compile` to resolve a Makefile circular dependency.

2. After the compiler fix, `make run` failed again: `qemu-aarch64: command not found`. Homebrew's `qemu` formula ships only `qemu-system-*` on macOS; user-mode QEMU is not portable to Darwin. ADR-2026-05-30-003 replaced the `QEMU` variable with a platform-aware `RUNNER`: Docker `--platform linux/arm64` on Darwin (Apple Virtualization, near-native ARMv8.3 execution), `qemu-aarch64` on Linux.

3. Milestone reached: `Hello, aarch64`, exit 0 via Docker.

**Phase 2 — PAC baseline**

4. User directed creation of a PAC-enabled sibling baseline. ADR-2026-05-30-004 created `baseline/aarch64-hello-pac/` with `-mbranch-protection=pac-ret`, `hello_corrupt.c` (inline asm LR clobber), and `verify-static` / `verify-trap` / `verify` targets.

5. Post-implementation three further defects were found and fixed in commit `3468643`:
   - GNU Make built-in `CC = cc` silently shadowed `CC ?= ...` in both Makefiles; fixed by changing to plain `CC =`
   - `verify-static` checked `.note.gnu.property` for the PAC bit, but static binaries do not emit this section (it is a dynamic-linker artifact); removed, keeping only the `objdump` disassembly check
   - `verify-trap` accepted only SIGILL (exit 132) and SIGSEGV (exit 139); Linux hardware PAC raises SIGBUS (signal 7, exit 135) on `autiasp` failure; added 135 to the accepted set

6. Milestone reached: `make verify` (both static + trap) and `make run` all green on `baseline/aarch64-hello-pac/`.

### Commits this session

| Commit | Message |
|---|---|
| `8629cdf` | fix: implement ADR-2026-05-30-002 — macOS toolchain and Makefile target rename |
| `189ac27` | feat: implement ADR-2026-05-30-003 — macOS QEMU user-mode workaround via Docker linux/arm64 |
| `8511f2a` | feat: implement ADR-2026-05-30-004 — PAC-enabled aarch64 hello-world baseline |
| `3468643` | fix: correct CC/READELF/OBJDUMP defaults and PAC verify-static/verify-trap |

### ADRs implemented this session

| ADR | Status |
|---|---|
| ADR-2026-05-30-002 — macOS toolchain and Makefile fix | Implemented |
| ADR-2026-05-30-003 — macOS QEMU user-mode workaround | Implemented |
| ADR-2026-05-30-004 — PAC-enabled baseline | Implemented |

### Key architectural insight documented this session

Apple Virtualization framework (used by Docker Desktop on Apple Silicon) exposes real ARMv8.3 PAC hardware to `linux/arm64` containers. PAC instructions execute with genuine hardware key enforcement — the CPU's internal APIASP key is active, and `autiasp` on a corrupted LR delivers SIGBUS to the process. QEMU user-mode would NOP these instructions. This means the Docker `linux/arm64` runner is not just a convenience workaround but the correct execution environment for hardware-authentic PAC analysis on macOS.

## Rationale / Decision Basis

The decision to create a separate `baseline/aarch64-hello-pac/` directory (rather than a flag variant) was driven by the need to keep the no-PAC baseline as an immutable control sample for future diff-based CFI bypass surface analysis. The `verify-static` + `verify-trap` two-phase verification pattern was established as a project convention: static checks prove the instructions are compiled in; the runtime trap test proves the runner enforces them.

## Impact

- **Enables:** CFI bypass surface analysis against a verified PAC-hardened binary on hardware-authentic execution. Both baselines are stable reference points.
- **Establishes conventions:** `RUNNER` abstraction; `compile`/`run`/`clean`/`verify-static`/`verify-trap`/`verify` target vocabulary; plain `CC =` (not `?=`) for cross-toolchain Makefiles in this project.
- **Key known limitations:** glibc in the static link is not PAC-signed (only our code's returns are protected); `hello_corrupt.c` LR offset is compiler-version-sensitive; Docker Desktop must be running for macOS execution.
- **Next expected work:** CFI bypass surface analyzer ADR targeting `baseline/aarch64-hello-pac/build/hello`.

## Related Records

- `records/2026-05-30_10-00_bug-fix_aarch64-hello-toolchain-and-circular-dep.md`
- `records/2026-05-30_10-15_adr_macos-toolchain-and-makefile-fix.md`
- `records/2026-05-30_11-00_bug-fix_qemu-aarch64-missing-on-macos.md`
- `records/2026-05-30_11-15_adr_macos-qemu-user-mode-workaround.md`
- `records/2026-05-30_12-00_milestone_no-pac-baseline-passing.md`
- `records/2026-05-30_12-30_directive_pac-baseline-requested.md`
- `records/2026-05-30_12-45_adr_pac-enabled-baseline.md`
- `records/2026-05-30_13-00_bug-fix_gnu-make-cc-default-and-verify-corrections.md`
- `records/2026-05-30_13-30_milestone_pac-baseline-passing.md`
