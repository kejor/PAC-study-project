# Milestone: ADR-2026-05-30-007 Explicit PAC Data Signing Test (t07) — Independent Verification Passed (16/16)

**Timestamp**: 2026-05-31 01:00 (UTC-5)
**Type**: Milestone
**Author/Source**: Agent (adr-implementation-tester)
**Tags**: milestone, pac, aarch64, explicit-data-signing, pacia, autia, inline-asm, cfi-bypass, vulnerability-testing, test-suite, adr-2026-05-30-007, independent-review, choi-ictc-2025, positive-control

## Summary

The adr-implementation-tester agent independently executed all 16 test cases — the full suite comprising t01–t06 (regressions) and the two new t07 variants (no-PAC and PAC-enabled) — and confirmed 16/16 passing on the first run with zero code changes required. The t07 PAC-enabled variant produced SIGSEGV (exit 139) on a corrupted `autia` pointer as specified, confirming explicit `pacia`/`autia` inline asm successfully detects data tampering. The t07 no-PAC variant printed `PWNED:t07` and exited 0 as expected. Static analysis via `make verify-static` confirmed that `pacia`/`autia` instructions are present in the PAC-enabled binary and absent from the no-PAC binary. All prior t01–t06 tests were unaffected. ADR-2026-05-30-007 is now fully closed: specified, implemented at commit `1077b1b`, and independently verified.

## Details

**Verification invocation:** `make test` from `tests/pac-vulnerabilities/` (macOS, Docker-backed QEMU user-mode execution environment, consistent with all prior suite verifications).

**Per-test results (16/16):**

| Test | Attack Class | no-PAC Result | PAC Result | Verified |
|------|-------------|---------------|------------|----------|
| t01  | Stack buffer overflow → `exploit_success_t01()` | `PWNED:t01`, exit 0 | exit 139 (SIGSEGV) | PASS |
| t02  | ret2func → `secret_admin()` | `PWNED:t02`, exit 0 | exit 139 (SIGSEGV) | PASS |
| t03  | 2-gadget ROP chain | `PWNED:t03`, exit 0 | exit 139 (SIGSEGV) | PASS |
| t04  | Off-by-one LR corruption (3-byte / 16 MiB window) | `PWNED:t04`, exit 0 | exit 139 (SIGSEGV) | PASS |
| t05a | Function-pointer overwrite (negative control) | `PWNED:t05a`, exit 0 | `PWNED:t05a`, exit 0 | PASS |
| t05b | Data-only attack (negative control) | `PWNED:t05b`, exit 0 | `PWNED:t05b`, exit 0 | PASS |
| t06  | SP-collision PAC pointer reuse (bypass) | `PWNED:t06`, exit 0 | `PWNED:t06`, exit 0 | PASS |
| t07  | Explicit data signing (positive control) | `PWNED:t07`, exit 0 | exit 139 (SIGSEGV) | PASS |

**Key verification result for t07 PAC-enabled variant:**

The PAC-enabled t07 binary exited with code 139 (SIGSEGV) and did not print `PWNED:t07` — the expected outcome per ADR-2026-05-30-007. The attack proceeds as follows:

1. `target.c` stores a security-critical data value, signing it with `pacia` inline asm using an instruction-key PAC tag.
2. The exploit overwrites the signed data value in memory with an attacker-controlled value — an unsigned or differently-tagged pointer.
3. On the subsequent read, `autia` authenticates the tag. The corrupted value fails authentication and the hardware raises a fault.
4. SIGSEGV (exit 139) terminates the process before `PWNED:t07` is printed. The attack fails.

This is the positive complement to t05b: where t05b shows that `-mbranch-protection=pac-ret` cannot protect data (attack succeeds), t07 shows that explicit `pacia`/`autia` can (attack fails). The t05b + t07 pairing cleanly bounds the data-protection coverage envelope of ARM PAC.

**Key verification result for t07 no-PAC variant:**

The no-PAC t07 binary printed `PWNED:t07` and exited 0. No signing or verification instructions are compiled in when `-DUSE_PAC_DATA_SIGNING` is absent. The overwrite is undetected, confirming that the protection is solely attributable to the explicit inline asm instrumentation — not to any ambient compiler flag.

**Static analysis verification (`make verify-static`):**

`verify-static` confirmed that `pacia` and `autia` instructions (matched via the widened grep patterns `paciasp|pacia` and `autiasp|autia` introduced during t07 implementation) are present in the PAC-enabled t07 binary and absent from the no-PAC binary. This rules out the possibility that the hardware trap was triggered by pre-existing `paciasp`/`autiasp` instrumentation unrelated to the explicit inline asm; the protection is directly attributable to the `pacia`/`autia` calls in `target.c`.

**No code changes required:** The implementation as committed at `1077b1b` was correct in all respects. The tester made no modifications to any source file, Makefile, harness script, or `expectations.yml`.

**Regression check:** t01–t04 continued to produce SIGSEGV (exit 139) on PAC-enabled builds; t05a/t05b continued to produce `PWNED` on both variants; t06 continued to produce `PWNED:t06` on the PAC-enabled build (demonstrating the SP-collision bypass). No regressions.

## Impact

- ADR-2026-05-30-007 is now fully closed: specified, implemented at commit `1077b1b`, and independently verified with zero code changes.
- The complete test suite (t01–t07, 16 cases) is accepted as the experimental backbone for the EE202C Project A CFI bypass surface analysis deliverable.
- t07 is the only test in the suite where a PAC-enabled build prevents a previously successful attack via explicit inline asm rather than compiler-inserted PAC instrumentation. This distinction (automatic `paciasp`/`autiasp` vs. deliberate `pacia`/`autia`) must be explained clearly in the project report and final presentation.
- The t05b + t07 pairing is now independently verified and fully citable: t05b demonstrates that `-mbranch-protection=pac-ret` alone provides no data-integrity protection; t07 demonstrates that Choi et al.'s explicit `pacia`/`autia` technique does. Together they define the data-protection boundary of ARM PAC and form a key empirical result for the EE202C deliverable.
- The widened `verify-static` grep patterns (`paciasp|pacia`, `autiasp|autia`) are confirmed to work correctly across the full 16-case suite with no false positives or missed detections. Future tests using bare `pacia`/`autia` should rely on these widened patterns.
- The `-march=armv8.3-a` compiler flag requirement (established during t07 implementation) is confirmed in practice: the bare `pacia`/`autia` instructions are ARMv8.3-A extensions and require this flag in any `Makefile` that exercises them.
- ADR-008 (t08, PAC oracle brute-force test) implementation is unblocked; the suite infrastructure it depends on is confirmed stable.

## Related Records

- `records/2026-05-30_20-00_adr_explicit-data-signing-test.md` — ADR-2026-05-30-007 specification
- `records/2026-05-31_00-00_milestone_t07-explicit-data-signing-implemented.md` — implementation milestone (commit `1077b1b`); handed off for this verification
- `records/2026-05-30_19-30_milestone_t06-sp-collision-independent-verification.md` — independent verification of t01–t06 (14/14 passing) that this record extends to 16 cases
- `records/2026-05-30_20-15_adr_pac-oracle-bruteforce-test.md` — ADR-2026-05-30-008 (t08), now unblocked
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, parent test suite spec
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — implementer/tester handoff pipeline
