# ADR: ADR-2026-05-30-004 — PAC-Enabled aarch64 Hello-World Baseline

**Timestamp**: 2026-05-30 12:45 (UTC-5)
**Type**: ADR
**Author/Source**: Agent (security-architect + adr-implementer)
**Tags**: adr, adr-implemented, pac, aarch64, cfi, hardware-security, docker, apple-silicon, verify-trap
**Commit**: `8511f2a`

## Summary

ADR-2026-05-30-004 was drafted and implemented to create `baseline/aarch64-hello-pac/` — an identical-source, PAC-enabled sibling to `baseline/aarch64-hello/`. The ADR introduces `-mbranch-protection=pac-ret`, a `hello_corrupt.c` binary for runtime trap verification, and two new Makefile targets (`verify-static`, `verify-trap`) that together prove PAC is both compiled in and actively enforced by the runner.

## Details

**New directory and files:**
- `baseline/aarch64-hello-pac/hello.c` — byte-identical to `baseline/aarch64-hello/hello.c`
- `baseline/aarch64-hello-pac/hello_corrupt.c` — non-leaf `victim()` function that uses inline asm to clobber the signed LR on the stack after `paciasp` but before `autiasp`, then attempts to return; on a PAC-enforcing runtime this raises SIGBUS/SIGILL
- `baseline/aarch64-hello-pac/Makefile` — based on ADR-003 runner abstraction, adds:
  - `PAC_FLAGS := -mbranch-protection=pac-ret` appended to `CFLAGS`
  - `CORRUPT` target (`build/hello_corrupt`) built from `hello_corrupt.c`
  - `RUNNER_CORRUPT` variable (same Docker/QEMU split as `RUNNER`, pointing to `/build/hello_corrupt`)
  - `verify-static` target: `$(OBJDUMP) -d $(TARGET) | grep -E "paciasp|autiasp"`
  - `verify-trap` target: runs `RUNNER_CORRUPT`, expects exit 132 (SIGILL, 128+4) or 139 (SIGSEGV, 128+11) as pass
  - `verify` target: runs `verify-static` then `verify-trap`
- `baseline/aarch64-hello-pac/README.md` — documents PAC flag rationale, runner PAC-exposure requirement, and bring-up log format

**ADR file:** `adr/ADR-2026-05-30-004-pac-enabled-baseline.md`

## Rationale / Decision Basis

- **Separate directory** over a flag variant in the existing Makefile: the no-PAC baseline must remain an immutable control sample; flag contamination risk is unacceptable. Also allows 1:1 ADR mapping for future hardening variants (BTI, PAC+BTI, MTE).
- **`pac-ret` only, not `standard` (PAC+BTI):** BTI requires whole-program coverage including `crt1.o`/`libc.a`. The `messense/macos-cross-toolchains` glibc is not BTI-compiled; mixing would produce subtle undefined behavior. PAC-ret is a per-function prologue/epilogue change with no whole-program requirement and matches the production deployment profile the analyzer will encounter (Android userspace, many Linux distros).
- **Runtime trap test is mandatory:** PAC instructions encode in the NOP hint space and are silently skipped on pre-ARMv8.3 hardware or in a non-enforcing runtime. Static disassembly proves the instructions are present; only a runtime authentication failure proves enforcement. The `hello_corrupt.c` + `verify-trap` pattern closes this gap definitively.
- **Docker `linux/arm64` is the correct macOS runner for PAC:** Apple Virtualization exposes real ARMv8.3 PAC hardware (with CPU-internal APIASP key) to the guest. QEMU user-mode, by contrast, would NOP PAC instructions even on Apple Silicon because qemu-user does not faithfully emulate PAC enforcement without a Linux kernel that has configured PAC keys.

## Impact

- Establishes `baseline/aarch64-hello-pac/` as the primary target artifact for CFI bypass surface analysis.
- The `verify-static` / `verify-trap` two-phase verification pattern is now the convention for all future hardening baselines.
- Proves that Apple Silicon + Docker Desktop `linux/arm64` is a viable, hardware-authentic PAC execution environment for this project.
- The inline asm LR clobber in `hello_corrupt.c` is compiler-version-sensitive (stack offset of signed LR under `-O0`); toolchain upgrades require re-verification of the offset.
- Note: this commit's `verify-trap` initially accepted only exit 132/139; a subsequent fix (`3468643`) added exit 135 (SIGBUS) after observing that Linux hardware PAC raises SIGBUS on authentication failure (see bug fix record).

## Related Records

- `records/2026-05-30_12-30_directive_pac-baseline-requested.md`
- `records/2026-05-30_13-00_bug-fix_gnu-make-cc-default-and-verify-corrections.md`
- `records/2026-05-30_13-30_milestone_pac-baseline-passing.md`
- `adr/ADR-2026-05-30-004-pac-enabled-baseline.md`
