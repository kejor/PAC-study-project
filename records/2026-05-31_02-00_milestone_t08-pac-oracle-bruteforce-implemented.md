# Milestone: ADR-2026-05-30-008 PAC Oracle Bruteforce Test (t08) Implemented — All 18 Cases Passing

**Timestamp**: 2026-05-31 02:00 (UTC-5)
**Type**: Milestone
**Author/Source**: Agent (adr-implementer)
**Commit**: `afe7885`
**Tags**: milestone, pac, aarch64, pac-oracle, bruteforce, pacman, sigsetjmp, siglongjmp, signal-handler, cfi-bypass, vulnerability-testing, test-suite, adr-2026-05-30-008, ravichandran-ieee-top-picks-2022, pac-width-discovery

## Summary

ADR-2026-05-30-008 has been fully implemented and committed at `afe7885`. All 18 test cases pass — the 16 cases from t01–t07 remain green, and the 2 new cases introduced by t08 (no-PAC and PAC-enabled variants) both produce `PWNED:t08` as specified. t08 is the most adversarially sophisticated test in the suite, implementing a software PAC oracle via `sigsetjmp`/`siglongjmp` signal handling to replicate the PACMAN attack concept (Ravichandran et al., MIT CSAIL; IEEE Micro Top Picks 2022) in a portable, QEMU-runnable form. Two documented deviations from the ADR spec were required: PAC-mask discovery uses a union of XORs across 33 modifier values (not a single XOR), and the effective PAC width in this environment is 7 bits (mask `0x007f000000000000`) rather than the textbook 16–24. The implementation adapts automatically via `brute_width = min(BRUTE_BITS, found)`. Implementation has been handed off to the adr-implementation-tester agent for independent verification.

## Details

**Files created/modified at commit `afe7885`:**

```
tests/pac-vulnerabilities/
  t08_pac_oracle_bruteforce/   (new)
    Makefile          — builds with -DBRUTE_BITS=8 default; accepts BRUTE_BITS override; -march=armv8.3-a required
    target.c          — victim function whose signed LR is the oracle target
    exploit.c         — sigsetjmp/siglongjmp oracle loop; SIGSEGV/SIGBUS handler; PAC-mask discovery; brute_width clamping
    README.md         — PACMAN oracle concept, software-signal adaptation, PAC-width deviation, and mask-discovery deviation documented
  harness/
    expectations.yml  — updated: t08 entry added for both pac_enforced and pac_disabled (both expect PWNED:t08)
```

**Test results summary (18/18 passing):**

| Test | Attack Class | no-PAC Result | PAC Result |
|------|-------------|---------------|------------|
| t01  | Stack buffer overflow → `exploit_success_t01()` | `PWNED:t01` | SIGSEGV (exit 139) |
| t02  | ret2func → `secret_admin()` | `PWNED:t02` | SIGSEGV (exit 139) |
| t03  | 2-gadget ROP chain | `PWNED:t03` | SIGSEGV (exit 139) |
| t04  | Off-by-one LR corruption (3-byte / 16 MiB window) | `PWNED:t04` | SIGSEGV (exit 139) |
| t05a | Function-pointer overwrite (negative control) | `PWNED:t05a` | `PWNED:t05a` |
| t05b | Data-only attack (negative control) | `PWNED:t05b` | `PWNED:t05b` |
| t06  | SP-collision PAC pointer reuse (bypass) | `PWNED:t06` | `PWNED:t06` |
| t07  | Explicit data signing (positive control) | `PWNED:t07` | SIGSEGV (exit 139) |
| t08  | PAC oracle bruteforce (PACMAN, software oracle) | `PWNED:t08` | `PWNED:t08` |

**Deviation 1 — PAC-mask discovery uses union of XORs across 33 modifier values:**

The ADR spec implied a single XOR between a signed pointer and its canonical form to identify which bits carry the PAC tag. In practice, a single XOR only exposed 4 of 7 PAC bits: bits that happened to match between the pointer value and the PAC tag stayed hidden because XOR of equal bits is zero. The implementation resolves this by iterating over 33 distinct modifier values (0x0000 through 0x0020), XOR-ing the signed pointer against the canonical form for each, and accumulating a union of all differing bits. This guarantees all PAC bits are exposed regardless of coincidental matches. The README documents this as a deviation from the spec.

**Deviation 2 — Effective PAC width is 7 bits in this environment:**

The textbook PAC field on ARMv8.3-A spans up to 16–24 bits depending on virtual address configuration. In the QEMU user-mode environment used by this test suite, the effective PAC width is only 7 bits, with mask `0x007f000000000000`. The ADR's default `BRUTE_BITS=8` sweep therefore over-estimates slightly but still terminates quickly. The implementation uses `brute_width = min(BRUTE_BITS, found)` to clamp the sweep to the discovered width, ensuring the brute-force loop never iterates beyond the actual PAC space. This is documented in `exploit.c` and `README.md`.

**`expectations.yml` addition:**

```yaml
t08:
  pac_enforced:
    sentinel_present: "PWNED:t08"   # PAC-ret does NOT protect: oracle bruteforce succeeds
  pac_disabled:
    sentinel_present: "PWNED:t08"
```

Both variants are expected to produce `PWNED:t08`. Under PAC-disabled, the first guess succeeds immediately (no auth tag to verify). Under PAC-enabled, the oracle exhausts up to `2^brute_width` guesses (at most 128 on this platform). This is the third test after t05a/t05b and t06 where the PAC-enabled build is expected to produce a PWNED sentinel.

**Attack algorithm as implemented:**

```
discover_pac_mask()        // union of XORs across 33 modifier values → mask
brute_width = min(BRUTE_BITS, popcount(mask))
install SIGSEGV/SIGBUS handler → siglongjmp(env, 1)
saved_lr = read_victim_saved_LR()
for guess in range(0, 2**brute_width):
    candidate = (saved_lr & ~mask) | (scatter(guess, mask))
    if sigsetjmp(env, 1) == 0:
        autia(candidate, modifier)     // attempt authentication
        print("PWNED:t08")             // correct guess; auth succeeded
        exit(0)
    // else: SIGSEGV/SIGBUS caught → siglongjmp fired → loop continues
```

**Key implementation constraints confirmed from ADR spec:**

- `sigsetjmp`/`siglongjmp` (not plain `setjmp`/`longjmp`) used throughout; signal mask must be saved and restored to prevent SIGSEGV from being blocked after the first catch.
- Both `SIGSEGV` and `SIGBUS` handlers installed; QEMU raises SIGSEGV on PAC auth failure while Apple Silicon raises SIGBUS.
- Signal handler is async-signal-safe: only calls `siglongjmp`, no allocation or non-reentrant functions.
- `-fno-stack-protector` applied (consistent with t01–t07).
- `-march=armv8.3-a` required (consistent with t07; bare `autia` is an ARMv8.3-A extension).

**Handoff status:** Implementation passed to adr-implementation-tester for independent verification of all 18 cases (both t08 variants plus regression confirmation of t01–t07).

## Impact

- `tests/pac-vulnerabilities/` now covers seven distinct attack/defense classes: stack buffer overflow (t01), ret2func (t02), ROP chain (t03), off-by-one LR corruption (t04), negative controls (t05a/t05b), SP-collision PAC pointer reuse (t06), explicit PAC data signing positive control (t07), and oracle-based PAC bruteforce (t08).
- t08 is the most adversarially sophisticated test in the suite and the only one grounded in a speculative execution / microarchitectural attack paper (IEEE Micro Top Picks 2022). Its software-oracle adaptation is a deliberate pedagogical simplification that preserves the logical structure of PACMAN without requiring hardware PMU access.
- The PAC-mask discovery deviation (union of 33 XORs) is a non-obvious but critical correctness fix. Future tests that need to locate PAC bits in a signed pointer should adopt the same union-of-XORs approach rather than a single XOR.
- The 7-bit effective PAC width (`0x007f000000000000`) is a concrete, measured property of this QEMU environment. It is relevant to any future test that reasons about PAC search space or brute-force feasibility, and should be cited in the EE202C Project A report when discussing attack complexity.
- `brute_width = min(BRUTE_BITS, found)` establishes the pattern for environment-adaptive sweep width. Future tests that iterate over PAC space should follow this pattern rather than hardcoding a bit count.
- Together with ADR-007 (t07), t08 completes the two paper-grounded ADRs that anchor the test suite to peer-reviewed literature: Choi et al. (ICTC 2025) covers explicit data signing; Ravichandran et al. (IEEE Top Picks 2022) covers oracle bruteforce. These are the two tests most directly citable in the final presentation.

## Related Records

- `records/2026-05-30_20-15_adr_pac-oracle-bruteforce-test.md` — ADR-2026-05-30-008 specification that this commit implements
- `records/2026-05-31_01-00_milestone_t07-explicit-data-signing-independent-verification.md` — independent verification of t01–t07 (16/16 passing); this milestone extends to 18 cases
- `records/2026-05-31_00-00_milestone_t07-explicit-data-signing-implemented.md` — implementation of t07 (commit `1077b1b`), the immediately preceding test; unblocked t08
- `records/2026-05-30_18-00_adr_sp-collision-pac-reuse-test.md` — ADR-2026-05-30-006 (t06), establishing the precedent for PAC-enabled PWNED expectations
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, parent test suite spec this extends
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — implementer/tester handoff pipeline this milestone feeds into
