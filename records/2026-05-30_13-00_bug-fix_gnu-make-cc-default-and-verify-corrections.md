# Bug Fix: GNU Make CC Default Shadowing and PAC Verify Corrections

**Timestamp**: 2026-05-30 13:00 (UTC-5)
**Type**: Bug Fix
**Author/Source**: User / Agent
**Tags**: bug-fix, makefile, gnu-make, toolchain, pac, verify-static, verify-trap, sigbus
**Commit**: `3468643`

## Summary

Three distinct defects were found and corrected after ADR-2026-05-30-004 was implemented: (1) GNU Make's built-in `CC=cc` default silently overrode `CC ?= ...` in both Makefiles, meaning the cross-compiler was never actually invoked; (2) `verify-static` attempted to check `.note.gnu.property` in a static binary, but this section is only emitted by the dynamic linker and absent from static ELFs; (3) `verify-trap` only accepted exit codes 132 (SIGILL) and 139 (SIGSEGV) but Linux hardware PAC raises SIGBUS (signal 7, exit 135) on authentication failure.

## Details

**Defect 1 — GNU Make CC default shadowing.**
GNU Make defines a built-in rule database that includes `CC = cc`. When a Makefile uses `CC ?=` (conditional assignment), it only sets `CC` if `CC` is not already defined. Because GNU Make's built-in `CC = cc` counts as "defined," the `?=` line was silently a no-op. The cross-toolchain gcc was never invoked; builds were falling through to the host `cc` (Apple Clang on macOS) or the first `cc` on `PATH` on Linux.

Fix: changed `CC ?= aarch64-unknown-linux-gnu-gcc` to plain `CC = aarch64-unknown-linux-gnu-gcc` in both `baseline/aarch64-hello/Makefile` and `baseline/aarch64-hello-pac/Makefile`. Same fix applied to `READELF` and `OBJDUMP` in the PAC Makefile. This means env-var overrides now require `make CC=...` on the command line (which overrides `=` assignments) rather than environment export (which does not override `=`).

**Defect 2 — `.note.gnu.property` absent in static binaries.**
The `verify-static` target originally ran:
```
$(READELF) -n $(TARGET) | grep -E "AArch64 feature: .*PAC"
```
The `.note.gnu.property` section containing `GNU_PROPERTY_AARCH64_FEATURE_1_AND` with the PAC bit is injected by the dynamic linker (`ld.so`) at load time to communicate CPU feature requirements to the kernel. Static binaries bypass `ld.so` entirely; the section is absent from the ELF. The grep was failing for this reason, not because PAC was absent from the code.

Fix: removed the `.note.gnu.property` check from `verify-static` entirely. The authoritative check for static PAC builds is `$(OBJDUMP) -d $(TARGET) | grep -E "paciasp|autiasp"` — the PAC instructions must be present in the disassembly. This check remains and was the only one needed.

**Defect 3 — Wrong signal for Linux hardware PAC trap.**
The ADR-004 implementation accepted exit 132 (`SIGILL`, 128+4) and 139 (`SIGSEGV`, 128+11) in `verify-trap` as evidence of PAC enforcement. On actual Linux hardware (including the ARMv8.3 hardware exposed by Apple Virtualization), a PAC authentication failure from `autiasp` raises **SIGBUS** (signal 7, bus error), yielding exit code 135 (128+7). The original expected signals were theoretically plausible for certain kernel configurations but incorrect for the hardware being used.

Fix: added exit code 135 to the accepted set in `verify-trap`:
```makefile
if [ $$status -eq 132 ] || [ $$status -eq 135 ] || [ $$status -eq 139 ]; then
```
Also updated the WARN message to reference `132/135/139`.

**Files modified by commit `3468643`:**
- `baseline/aarch64-hello-pac/Makefile`: `CC =`, `READELF =`, `OBJDUMP =`; `verify-static` sans `.note.gnu.property` check; `verify-trap` with exit 135 added
- `baseline/aarch64-hello/Makefile`: `CC =` only (no PAC-specific changes)

## Rationale / Decision Basis

The GNU Make `?=` vs `=` semantic is a subtle well-known gotcha: most developers expect `?=` to behave like "default if not set by the user" but it also loses to Make's internal defaults. The correction to `=` is correct for this use case because the cross-triple is mandatory — there is no meaningful fallback to `cc` on any platform in this project.

The `.note.gnu.property` finding is an important clarification for future work: this section cannot be used to verify PAC presence in statically-linked binaries. Disassembly is the only reliable static check.

The SIGBUS discovery is the most security-relevant finding: it confirms that the hardware PAC enforcement path on Linux/Apple Virtualization is `SIGBUS`, not `SIGILL`. This matches the ARMv8.3 specification (the `PSTATE.TCO` bit and kernel signal delivery path for PAC failures are defined to raise `SIGBUS` in the Linux kernel source).

## Impact

All three fixes were required before `make verify` could pass. Without the `CC =` fix, both baselines were building with host clang and the PAC binary was a macOS Mach-O or mis-targeted ELF, not a real aarch64 Linux PAC binary. Without the `verify-trap` signal fix, the test would have reported WARN and exited non-zero even on a correctly-enforcing system.

These corrections also sharpen the project's understanding of the execution environment: the canonical PAC trap signal for this stack is SIGBUS/exit 135.

## Related Records

- `records/2026-05-30_12-45_adr_pac-enabled-baseline.md`
- `records/2026-05-30_13-30_milestone_pac-baseline-passing.md`
