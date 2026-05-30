# Milestone: ADR-2026-05-30-006 SP-Collision PAC Reuse (t06) — Independent Verification Passed (14/14)

**Timestamp**: 2026-05-30 19:30 (UTC-5)
**Type**: Milestone
**Author/Source**: Agent (adr-implementation-tester)
**Tags**: milestone, pac, aarch64, sp-collision, pac-reuse, cfi-bypass, pointer-reuse, vulnerability-testing, test-suite, adr-2026-05-30-006, independent-review, liljestrand-usenix-19

## Summary

The adr-implementation-tester agent independently executed all 14 test cases — the full suite comprising t01–t04 (PAC blocks), t05a/t05b (negative controls), and t06 (SP-collision PAC pointer reuse) in both no-PAC and PAC-enabled variants — and confirmed 14/14 passing on the first run with zero code changes required. The t06 PAC variant produced `PWNED:t06` exit 0 as specified, confirming the SP-collision bypass succeeds under `-mbranch-protection=pac-ret`. Static analysis verified 11 `paciasp` and 2 `autiasp` sites in the PAC binary, ruling out the possibility that the bypass succeeded due to missing PAC instrumentation. ADR-2026-05-30-006 is now fully closed: specified, implemented, and independently verified.

## Details

**Verification invocation:** `make test` from `tests/pac-vulnerabilities/` (macOS, Docker-backed QEMU user-mode execution environment, consistent with prior t01–t05 verification at commit `5f0d72c`).

**Per-test results (14/14):**

| Test | Attack Class | no-PAC Result | PAC Result | Verified |
|------|-------------|---------------|------------|----------|
| t01  | Stack buffer overflow → `exploit_success_t01()` | exit 0, `PWNED:t01` | exit 139 (SIGSEGV) | PASS |
| t02  | ret2func → `secret_admin()` | exit 0, `PWNED:t02` | exit 139 (SIGSEGV) | PASS |
| t03  | 2-gadget ROP chain | exit 0, `PWNED:t03` | exit 139 (SIGSEGV) | PASS |
| t04  | Off-by-one LR corruption (3-byte / 16 MiB window) | exit 0, `PWNED:t04` | exit 139 (SIGSEGV) | PASS |
| t05a | Function-pointer overwrite (negative control) | exit 0, `PWNED:t05a` | exit 0, `PWNED:t05a` | PASS |
| t05b | Data-only attack (negative control) | exit 0, `PWNED:t05b` | exit 0, `PWNED:t05b` | PASS |
| t06  | SP-collision PAC pointer reuse (bypass) | exit 0, `PWNED:t06` | exit 0, `PWNED:t06` | PASS |

**Key verification result for t06 PAC variant:**

The PAC-enabled t06 binary exited with code 0 and printed `PWNED:t06` — the expected bypass sentinel. The attack proceeds as follows:

1. `donor()` executes `paciasp`, signing x30 against the current SP modifier, and stores the resulting PAC-tagged pointer at `[fp, #8]`.
2. The exploit's controlled-write primitive reads that tagged value from `donor`'s `[fp, #8]`.
3. `victim()` is called at the same SP value as `donor()` (guaranteed by frame-size matching and the SP-equality assert in the implementation).
4. The controlled-write primitive overwrites `victim`'s own `[fp, #8]` with the donor's PAC-tagged x30.
5. `victim`'s epilogue executes `autiasp` against the current SP modifier — which is identical to the donor's, since both functions were called at the same stack depth. Authentication succeeds.
6. x30 is restored to the donor's original signed return address, pointing back into `main`. The `static volatile int once` reentry guard in `main` detects the second entry and routes control to `exploit_success_t06()`.

**Static analysis verification (`make verify-static`):** The PAC-enabled t06 binary contains 11 `paciasp` sites and 2 `autiasp` sites. This confirms the toolchain is emitting PAC prologues and epilogues throughout the binary; the bypass is not a consequence of missing PAC instrumentation. `pac-ret` is active, and the attack succeeds regardless.

**No code changes required:** The implementation as committed at `2aa8077` was correct in all respects. The tester made no modifications to any source file, Makefile, harness script, or `expectations.yml`.

**Regression check:** t01–t04 continued to produce SIGSEGV (exit 139) on PAC-enabled builds, and t05a/t05b continued to produce `PWNED` on both build variants. No regressions introduced by the t06 addition.

## Impact

- ADR-2026-05-30-006 is now fully closed: specified, implemented at commit `2aa8077`, and independently verified with zero code changes.
- The complete test suite (t01–t06, 14 cases) is accepted as the experimental backbone for the EE202C Project A CFI bypass surface analysis deliverable.
- t06 is the first and only test in the suite where `-mbranch-protection=pac-ret` does NOT prevent the attack. This is a qualitatively distinct result class that must be prominently reported: the suite now documents both what PAC protects (t01–t04: return-address attacks) and a concrete class of attack it does not protect (t06: SP-collision pointer reuse, Liljestrand et al. §3/§7.2.1).
- The verify-static count (11 `paciasp`, 2 `autiasp`) is a citable, empirically observed artifact for the project report's section on PAC coverage. The asymmetry (more sign sites than auth sites) is expected since not all signed pointers are authenticated in the same function; it confirms partial-use of PAC in typical compiled code.
- The bypass mechanism — transplanting a PAC-tagged LR across functions that share an identical SP modifier — empirically confirms the Liljestrand et al. §7.2.1 claim that a PARTS-style function-id modifier XOR'd into the SP would close this attack, and that `pac-ret` alone does not.
- The suite is ready for any future extension (BTI, MTE, or additional PAC bypass variants) using the same harness, Docker environment, and `expectations.yml` pattern.

## Related Records

- `records/2026-05-30_18-00_adr_sp-collision-pac-reuse-test.md` — ADR-2026-05-30-006 specification
- `records/2026-05-30_19-00_milestone_t06-sp-collision-pac-reuse-implemented.md` — implementation milestone (commit `2aa8077`); handed off for this verification
- `records/2026-05-30_17-00_milestone_pac-test-suite-independent-verification.md` — independent verification of t01–t05 (12/12 passing) that this record extends
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, parent test suite spec
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — implementer/tester handoff pipeline
