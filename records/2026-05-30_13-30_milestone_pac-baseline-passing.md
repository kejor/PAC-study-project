# Milestone: PAC-Enabled aarch64 Baseline Fully Passing

**Timestamp**: 2026-05-30 13:30 (UTC-5)
**Type**: Milestone
**Author/Source**: User / Agent
**Tags**: milestone, pac, aarch64, verify-trap, verify-static, docker, apple-silicon, hardware-pac

## Summary

`make verify` (covering both `verify-static` and `verify-trap`) and `make run` all passed green on `baseline/aarch64-hello-pac/`. This milestone confirms that ARMv8.3 PAC is not only compiled into the binary but is actively enforced by the Docker `linux/arm64` runner on Apple Silicon — the deliberate LR corruption in `hello_corrupt` triggered a hardware authentication failure (SIGBUS, exit 135), not a silent pass-through. Both baselines are now fully operational.

## Details

**Verification results at milestone:**

| Target | Result | Evidence |
|---|---|---|
| `make compile` | PASS | `build/hello` and `build/hello_corrupt` produced; both static aarch64 ELFs |
| `make verify-static` | PASS | `objdump -d build/hello` contains `paciasp` and `autiasp` instructions |
| `make run` | PASS | Docker `linux/arm64` printed `Hello, aarch64`, exit 0 |
| `make verify-trap` | PASS | `hello_corrupt` raised SIGBUS (exit 135); "PAC NOT enforced" line never printed |
| `make verify` | PASS | Sequential run of `verify-static` + `verify-trap`, both green |

**Key confirmation:** The `verify-trap` SIGBUS (exit 135) result is hardware-authentic — the Apple Virtualization framework passed real ARMv8.3 PAC key context to the Linux guest, the kernel enforced authentication on `autiasp`, and delivered SIGBUS to the process. This is the correct behavior; it distinguishes real PAC enforcement from NOP-space hint execution.

**State of both baselines at this milestone:**
- `baseline/aarch64-hello/` — no-PAC control sample, `make run` passing (Docker linux/arm64), locked
- `baseline/aarch64-hello-pac/` — PAC target sample, `make verify` + `make run` passing

**Final commit implementing the passing state:** `3468643` (CC default fix + verify corrections)

## Impact

- Both baselines are operational and locked. `baseline/aarch64-hello/` serves as the no-PAC control; `baseline/aarch64-hello-pac/` is the PAC target for CFI bypass surface analysis.
- The project has a verified hardware PAC execution environment on macOS Apple Silicon, which is a prerequisite for meaningful bypass surface analysis.
- The `verify-static` / `verify-trap` convention is proven and ready to be adopted by future hardening baselines (BTI, PAC+BTI, MTE).
- The canonical PAC trap signal for this execution stack is confirmed: **SIGBUS (exit 135)**, not SIGILL or SIGSEGV.
- Unblocks the next phase of the EE202C project: CFI bypass surface analysis tooling targeting the PAC-enabled binary.

## Related Records

- `records/2026-05-30_13-00_bug-fix_gnu-make-cc-default-and-verify-corrections.md`
- `records/2026-05-30_12-45_adr_pac-enabled-baseline.md`
- `records/2026-05-30_12-00_milestone_no-pac-baseline-passing.md`
