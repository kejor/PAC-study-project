# Milestone: ADR-2026-05-30-007 Explicit PAC Data Signing Test (t07) Implemented — All 16 Cases Passing

**Timestamp**: 2026-05-31 00:00 (UTC-5)
**Type**: Milestone
**Author/Source**: Agent (adr-implementer)
**Commit**: `1077b1b`
**Tags**: milestone, pac, aarch64, explicit-data-signing, pacia, autia, inline-asm, cfi-bypass, vulnerability-testing, test-suite, adr-2026-05-30-007, choi-ictc-2025, positive-control

## Summary

ADR-2026-05-30-007 has been fully implemented and committed at `1077b1b`. All 16 test cases pass — the 14 cases from t01–t06 remain green, and the 2 new cases introduced by t07 (no-PAC and PAC-enabled variants) both produce their expected outcomes as specified. t07 is the first test in the suite that demonstrates explicit `pacia`/`autia` inline assembly CAN protect arbitrary data values, forming the positive complement to t05b (negative control showing that `-mbranch-protection=pac-ret` alone cannot protect data). This result is grounded in Choi et al., "High-Performance and Secure Data Integrity Verification using ARM Pointer Authentication Code" (ICTC 2025). Implementation has been handed off to the adr-implementation-tester agent for independent verification; ADR-008 was dispatched to adr-implementer in parallel.

## Details

**Files created/modified at commit `1077b1b`:**

```
tests/pac-vulnerabilities/
  t07_explicit_data_signing/   (no-PAC and PAC variants, new)
    Makefile
    target.c          — stores data value via pacia; verifies with autia on read
    exploit.c         — overwrites the signed data value in memory
    README.md         — Choi et al. technique explanation and relation to t05b
  harness/
    expectations.yml  — updated: t07 entry added for both pac_enforced and pac_disabled
```

**Test results summary (16/16 passing):**

| Test | Attack Class | no-PAC Result | PAC Result |
|------|-------------|---------------|------------|
| t01  | Stack buffer overflow → `exploit_success_t01()` | `PWNED:t01` | SIGSEGV (exit 139) |
| t02  | ret2func → `secret_admin()` | `PWNED:t02` | SIGSEGV (exit 139) |
| t03  | 2-gadget ROP chain | `PWNED:t03` | SIGSEGV (exit 139) |
| t04  | Off-by-one LR corruption (3-byte / 16 MiB window) | `PWNED:t04` | SIGSEGV (exit 139) |
| t05a | Function-pointer overwrite (negative control) | `PWNED:t05a` | `PWNED:t05a` |
| t05b | Data-only attack (negative control) | `PWNED:t05b` | `PWNED:t05b` |
| t06  | SP-collision PAC pointer reuse (bypass) | `PWNED:t06` | `PWNED:t06` |
| t07  | Explicit data signing (positive control) | `PWNED:t07` | PAC auth failure (SIGILL/SIGSEGV/SIGBUS) |

**Key implementation constraints resolved during implementation:**

**`-march=armv8.3-a` compiler flag required:** The bare `pacia` and `autia` instructions (not the `paciasp`/`autiasp` SP-modifier variants) are ARMv8.3-A extensions. Without `-march=armv8.3-a` the cross-compiler (`aarch64-linux-gnu-gcc`) rejects the inline assembly with an "instruction not supported" error. This flag was added to the t07 `Makefile` for both the no-PAC and PAC-enabled build targets. Note that `-mbranch-protection` is NOT used here — it does not gate inline asm.

**`verify-static` grep pattern widened:** The static analysis step used `paciasp` and `autiasp` as grep patterns to count PAC sites in the compiled binary. For t07's PAC-enabled variant, the `main` function never returns (the exploit path terminates before the epilogue is reached), so the compiler can legitimately elide the `autiasp` epilogue — causing the original grep to report 0 `autiasp` hits and fail the static check. The patterns were widened to `paciasp|pacia` and `autiasp|autia` to correctly detect both the SP-modifier variants (emitted by `-mbranch-protection=pac-ret` throughout the binary) and the explicit inline `pacia`/`autia` instructions introduced by t07. The widened patterns correctly count PAC sites in all prior tests (t01–t06) unchanged.

**`expectations.yml` addition:**

```yaml
t07:
  pac_enforced:
    exit_signals: [SIGBUS, SIGILL, SIGSEGV]
    sentinel_absent: "PWNED:t07"
  pac_disabled:
    sentinel_present: "PWNED:t07"
```

The harness correctly handles the PAC-enabled variant producing a signal exit (not exit 0) as a PASS, consistent with t01–t04 behavior. No harness logic changes were required beyond the `expectations.yml` entry.

**t07 vs. t05b distinction (critical for report):** t05b and t07 use the same attacker primitive (direct memory overwrite of a data value), but differ in the defense layer:
- t05b: protection is attempted via `-mbranch-protection=pac-ret` only. The flag does not sign data. Attack succeeds (`PWNED:t05b` printed even in PAC build).
- t07: protection is applied via explicit `pacia` inline asm at the call site and `autia` at the verification point. The hardware tag mismatch on authentication raises a fault. Attack fails (signal raised, `PWNED:t07` absent).

Together, t05b and t07 bound the data-protection envelope of ARM PAC: automatic PAC (`-mbranch-protection`) cannot protect data; explicit `pacia`/`autia` can.

**Handoff status:** Implementation passed to adr-implementation-tester for independent verification of all 16 cases (both t07 variants plus regression confirmation of t01–t06). ADR-008 (t08, PAC oracle brute-force test) was dispatched to adr-implementer in parallel with the handoff.

## Impact

- `tests/pac-vulnerabilities/` now covers six distinct attack/defense classes: stack buffer overflow (t01), ret2func (t02), ROP chain (t03), off-by-one LR corruption (t04), negative controls (t05a/t05b), SP-collision PAC pointer reuse (t06), and explicit PAC data signing positive control (t07).
- t07 is the first test in the suite that exercises explicit inline-asm PAC instructions (`pacia`/`autia`) rather than relying on compiler-inserted `paciasp`/`autiasp`. This is a qualitatively different instrumentation path and must be explained clearly in the EE202C Project A report.
- The `-march=armv8.3-a` requirement is now documented: any future test using bare `pacia`/`autia`/`pacib`/`autib` (not the SP-modifier variants) must include this flag in its `Makefile`.
- The widened `verify-static` grep patterns (`paciasp|pacia`, `autiasp|autia`) are now the established suite-wide standard. Future tests should use these widened patterns rather than the narrower SP-modifier-only forms.
- The t05b + t07 pairing provides a citable, runnable dual demonstration for the project report: one test shows PAC's data coverage gap, the other shows how to close it. This is directly aligned with the Choi et al. (ICTC 2025) contribution being cited.
- ADR-008 (t08) implementation is now unblocked pending adr-implementer pickup.

## Related Records

- `records/2026-05-30_20-00_adr_explicit-data-signing-test.md` — ADR-2026-05-30-007 specification that this commit implements
- `records/2026-05-30_19-30_milestone_t06-sp-collision-independent-verification.md` — independent verification of t01–t06 (14/14 passing); this milestone extends to 16 cases
- `records/2026-05-30_19-00_milestone_t06-sp-collision-pac-reuse-implemented.md` — implementation of t06 (commit `2aa8077`), the immediately preceding test
- `records/2026-05-30_20-15_adr_pac-oracle-bruteforce-test.md` — ADR-2026-05-30-008 (t08), dispatched to adr-implementer in parallel with this handoff
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, parent test suite spec this extends
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — implementer/tester handoff pipeline this milestone feeds into
