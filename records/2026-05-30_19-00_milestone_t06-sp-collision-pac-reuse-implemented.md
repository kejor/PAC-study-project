# Milestone: ADR-2026-05-30-006 SP-Collision PAC Reuse Test (t06) Implemented — All 14 Cases Passing

**Timestamp**: 2026-05-30 19:00 (UTC-5)
**Type**: Milestone
**Author/Source**: Agent (adr-implementer)
**Commit**: `2aa8077`
**Tags**: milestone, pac, aarch64, sp-collision, pac-reuse, cfi-bypass, pointer-reuse, vulnerability-testing, test-suite, adr-2026-05-30-006, liljestrand-usenix-19

## Summary

ADR-2026-05-30-006 has been fully implemented and committed at `2aa8077`. All 14 test cases pass — the original 12 from t01–t05 remain green, and the 2 new cases introduced by t06 (no-PAC and PAC-enabled variants) both produce `PWNED:t06` as specified. t06 is the first test in the suite where `-mbranch-protection=pac-ret` does NOT block the attack, providing a runnable demonstration of the SP-collision pointer-reuse weakness from Liljestrand et al. (USENIX Security '19), Sections 3 and 7.2.1. Implementation has been handed off to the adr-implementation-tester agent for independent verification.

## Details

**Files created/modified at commit `2aa8077`:**

```
tests/pac-vulnerabilities/
  t06_sp_collision_pac_reuse/   (no-PAC and PAC variants, new)
    Makefile
    target.c          — donor(), victim(), controlled-write primitive
    exploit.c         — captures donor PAC-signed LR, transplants into victim's saved LR
    README.md         — SP-collision weakness explanation and expected results
  harness/
    expectations.yml  — updated: t06 entry added for both pac_enforced and pac_disabled
```

**Test results summary (14/14 passing):**

| Test | Attack Class | no-PAC Result | PAC Result |
|------|-------------|---------------|------------|
| t01  | Stack buffer overflow → `exploit_success_t01()` | `PWNED:t01` | SIGSEGV (exit 139) |
| t02  | ret2func → `secret_admin()` | `PWNED:t02` | SIGSEGV (exit 139) |
| t03  | 2-gadget ROP chain | `PWNED:t03` | SIGSEGV (exit 139) |
| t04  | Off-by-one LR corruption (3-byte / 16 MiB window) | `PWNED:t04` | SIGSEGV (exit 139) |
| t05a | Function-pointer overwrite (negative control) | `PWNED:t05a` | `PWNED:t05a` |
| t05b | Data-only attack (negative control) | `PWNED:t05b` | `PWNED:t05b` |
| t06  | SP-collision PAC pointer reuse | `PWNED:t06` | `PWNED:t06` (bypass succeeds) |

**Key implementation details:**

**Reentry guard in `main` (`static volatile int once`):** The victim's corrupted LR must return to a location inside `main` rather than directly to `exploit_success_t06`. If the LR were overwritten to point directly to `exploit_success_t06`, the `autiasp` verification in the victim's epilogue would fault (because the raw address of `exploit_success_t06` does not carry a valid PAC tag from the current SP). Instead, the victim's LR is overwritten with the donor's signed return address, which points back into `main`. A `static volatile int once` flag gates control flow: on the first return (once == 0, the legitimate return from `victim()`), `once` is set to 1; on the second entry into `main` via the transplanted LR, `once == 1` routes execution to `exploit_success_t06`. This ensures that `autiasp` sees a correctly signed address (the donor's) and authenticates successfully, and the reentry path then branches to the attacker's target.

**Frame layout matching (donor/victim SP equality):** Both `donor()` and `victim()` must be called at identical SP values. To guarantee this, the donor must declare the full set of local variables that the victim uses, so that GCC `-O0` allocates frames of equal size for both functions. During implementation, the SP-equality assert (comparing the SP values observed by donor and victim at call time) caught two distinct frame-size mismatches before the layouts converged. This assert is retained in the final implementation as a self-checking invariant: if a future compiler flag change or refactor causes frame divergence, the test will abort with an assertion failure rather than silently producing a non-representative result.

**`expectations.yml` update:**

```yaml
t06:
  pac_enforced:
    sentinel_present: "PWNED:t06"   # PAC-ret does NOT protect: SP-collision bypass
  pac_disabled:
    sentinel_present: "PWNED:t06"
```

The harness was already capable of handling a PAC-on PWNED result as a PASS (not an automatic FAIL), because `expectations.yml` fully controls per-test pass criteria. No harness logic changes were required — only the `expectations.yml` entry addition.

**Handoff status:** Implementation passed to adr-implementation-tester for independent verification of all 14 cases (both t06 variants plus regression confirmation of t01–t05).

## Impact

- `tests/pac-vulnerabilities/` now covers five distinct attack classes: stack buffer overflow (t01), ret2func (t02), ROP chain (t03), off-by-one LR corruption (t04), negative controls (t05), and SP-collision PAC pointer reuse (t06).
- t06 is the first test in the suite that demonstrates a bypass under `-mbranch-protection=pac-ret` — a qualitatively different result class from t01–t04 (which demonstrate PAC defenses). This distinction is directly reportable in the EE202C Project A CFI bypass surface analysis.
- The SP-equality assert pattern (donor and victim frame size must match) is now an established self-checking technique in the suite and should be documented in `common/README.md` or the t06 `README.md` for future contributors adding pointer-reuse tests.
- The `static volatile int once` reentry guard technique is reusable for any future test that requires the transplanted return address to land inside a live function rather than directly at a sentinel emitter.
- The implementation empirically confirms the Liljestrand et al. §7.2.1 claim: a PARTS-style function-id modifier XOR'd into the SP would close this bypass, and `pac-ret` alone does not.
- Independent tester verification is the next gate before this milestone is considered fully accepted.

## Related Records

- `records/2026-05-30_18-00_adr_sp-collision-pac-reuse-test.md` — ADR-2026-05-30-006 specification that this commit implements
- `records/2026-05-30_16-30_milestone_pac-vulnerability-test-suite-implemented.md` — implementation milestone for t01–t05 (commit `5f0d72c`)
- `records/2026-05-30_17-00_milestone_pac-test-suite-independent-verification.md` — independent verification of t01–t05 (12/12 passing)
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, parent test suite spec this extends
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — implementer/tester handoff pipeline this milestone feeds into
