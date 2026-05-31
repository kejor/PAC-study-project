# ADR: ADR-2026-05-30-008 — PAC Oracle Bruteforce Test (t08)

**Timestamp**: 2026-05-30 20:15 (UTC-5)
**Type**: ADR
**Author/Source**: Agent (security-architect)
**Status**: Ready (sent to adr-implementer)
**Tags**: adr, pac, aarch64, pacman, pac-oracle, bruteforce, speculative-execution, signal-handler, sigsetjmp, siglongjmp, cfi-bypass, test-suite, ieee-top-picks-2022

## Summary

ADR-2026-05-30-008 specifies a new test `t08_pac_oracle_bruteforce` to be added to the `tests/pac-vulnerabilities/` suite. The test is grounded in Ravichandran et al., "PACMAN: Attacking ARM Pointer Authentication With Speculative Execution" (MIT; IEEE Top Picks 2022). It demonstrates the PACMAN PAC Oracle concept in software: rather than using microarchitectural speculative execution to suppress fault visibility, the test uses a `sigsetjmp`/`siglongjmp` signal handler to catch SIGSEGV/SIGBUS on a wrong PAC guess and iterate to the next candidate. A correct guess survives `autia` and allows execution to continue normally. The default sweep covers 8 bits (256 iterations); a full 24-bit sweep is available via `-DBRUTE_BITS=24`. Both PAC-enabled and PAC-disabled variants are expected to print `PWNED:t08`. ADR-007 must be implemented before this ADR due to a Makefile `BUILD_RULE` dependency.

## Details

**Academic basis:**
Ravichandran et al., "PACMAN: Attacking ARM Pointer Authentication With Speculative Execution," MIT CSAIL; IEEE Micro Top Picks 2022. The paper introduces the PAC Oracle primitive: an attacker can determine whether a guessed PAC tag is correct by observing whether `autia` produces a fault. PACMAN exploits speculative execution to make this observation without a visible crash. This test replaces the microarchitectural channel with a software signal handler (`sigsetjmp`/`siglongjmp`) to demonstrate the same oracle logic in a controlled, portable environment.

**Attack algorithm:**

```
saved_lr = read_victim_saved_LR()           // via stack disclosure primitive
for guess in range(0, 2**BRUTE_BITS):
    candidate = forge_pac(saved_lr, guess)   // insert guessed PAC bits
    if setjmp(env) == 0:
        autia(candidate)                     // attempt authentication
        // if we reach here, guess was correct
        print("PWNED:t08")
        exit(0)
    else:
        continue                             // SIGSEGV/SIGBUS caught, try next
```

The signal handler is installed with `sigaction(SIGSEGV, ...)` and `sigaction(SIGBUS, ...)`. On authentication failure, the hardware raises a fault; the handler calls `siglongjmp(env, 1)` to resume the loop at the next guess. On the correct guess, `autia` succeeds and the loop body continues to the exploit payload.

**Expected outcomes:**

| Variant | Expected outcome |
|---------|-----------------|
| PAC enabled (`-mbranch-protection=pac-ret`) | `PWNED:t08` printed — oracle bruteforce succeeds |
| PAC disabled | `PWNED:t08` printed — trivially succeeds without guessing |

Both variants are expected to succeed. Under PAC-enabled, success requires exhausting up to 2^BRUTE_BITS guesses; under PAC-disabled, the first attempt succeeds immediately (no auth tag to verify).

**`-DBRUTE_BITS` parameter:**

| Value | Iterations | Notes |
|-------|-----------|-------|
| 8 (default) | 256 | Fast; sweeps the low byte of the PAC field; suitable for CI |
| 24 | 16,777,216 | Full PAC field sweep on a 24-bit effective tag; slow but thorough |

The default 8-bit sweep is sufficient to demonstrate the oracle concept and is fast enough for automated test runs. `-DBRUTE_BITS=24` is provided for presentations or experiments that require a full exhaustive sweep.

**Planned test layout:**

```
tests/pac-vulnerabilities/t08/
  Makefile        — builds with -DBRUTE_BITS=8 default; accepts BRUTE_BITS override
  target.c        — victim function whose signed LR is the target
  exploit.c       — sigsetjmp/siglongjmp oracle loop; installs SIGSEGV/SIGBUS handler
  README.md       — explains PACMAN oracle concept and software-signal adaptation
```

**`expectations.yml` addition:**

```yaml
t08:
  pac_enforced:
    sentinel_present: "PWNED:t08"   # PAC-ret does NOT protect: oracle bruteforce succeeds
  pac_disabled:
    sentinel_present: "PWNED:t08"
```

**Key implementation constraints:**

- `sigsetjmp`/`siglongjmp` (not plain `setjmp`/`longjmp`) are required because the signal handler must restore the signal mask on return to the loop.
- Both `SIGSEGV` and `SIGBUS` handlers must be installed; different platforms (QEMU/Linux vs. Apple Silicon) raise different signals on PAC auth failure.
- The signal handler must be async-signal-safe: it may only call `siglongjmp` and must not allocate memory or call non-reentrant functions.
- `-fno-stack-protector` is mandatory (consistent with t01–t07).
- The `PWNED:t08` sentinel follows suite-wide convention.
- This test has a Makefile `BUILD_RULE` dependency on t07 (ADR-2026-05-30-007); t07 must be implemented first.

## Rationale / Decision Basis

- **Software signal handler as oracle substitute:** PACMAN's microarchitectural oracle is not reproducible in a portable software test without specific hardware. Replacing it with a `sigsetjmp`/`siglongjmp` handler preserves the logical structure of the oracle (try a guess, observe fault-or-no-fault, iterate) while making it runnable in QEMU and on Apple Silicon without privilege escalation or hardware PMU access.
- **Both variants expected PWNED:** This is the second test (after t06) where PAC-enabled is expected to be bypassed. The `expectations.yml` entry must reflect this explicitly to prevent the harness from misclassifying a correct result as a failure.
- **8-bit default for CI speed:** A 24-bit sweep takes O(16M) iterations and is impractical for automated runs. The 8-bit default exercises the oracle concept and completes in milliseconds, while `-DBRUTE_BITS=24` is preserved for explicit demonstration.
- **Grounded in IEEE Top Picks literature:** Ravichandran et al. (IEEE Micro Top Picks 2022) is a high-impact, peer-reviewed source. Citing it as the basis for t08 connects the EE202C deliverable to a recognized result in the hardware security community.
- **Sequential ordering (after t07):** The Makefile `BUILD_RULE` dependency requires t07 to be present before t08 can be built. The adr-implementer must implement ADR-007 before starting ADR-008.

## Impact

- Extends `tests/pac-vulnerabilities/` with an oracle-based bruteforce attack class — the most adversarially sophisticated test in the suite.
- Provides a citable, runnable demonstration of the PACMAN oracle concept (in software form) for the EE202C Project A report and final presentation.
- The `expectations.yml` entry for t08 requires the harness to handle a second case where PAC-enabled prints a PWNED sentinel; this pattern was first established by t06.
- Completes the planned test suite expansion through the two paper-grounded ADRs (007 and 008); together they cover both the affirmative PAC data-signing result (Choi et al.) and the oracle bruteforce bypass (Ravichandran et al.).
- adr-implementer is responsible for scaffolding `tests/pac-vulnerabilities/t08/` and updating `expectations.yml`. adr-implementation-tester must verify both PAC-on and PAC-off variants print `PWNED:t08` before the test is considered done.

## Related Records

- `records/2026-05-30_20-00_adr_explicit-data-signing-test.md` — ADR-2026-05-30-007 (t07), which must be implemented first (Makefile BUILD_RULE dependency)
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, the parent test suite spec this test extends
- `records/2026-05-30_18-00_adr_sp-collision-pac-reuse-test.md` — ADR-2026-05-30-006 (t06), establishing the precedent for PAC-enabled PWNED expectations
- `records/2026-05-30_16-30_milestone_pac-vulnerability-test-suite-implemented.md` — milestone confirming t01–t05 are implemented
- `records/2026-05-30_17-00_milestone_pac-test-suite-independent-verification.md` — independent verification of t01–t05
- `records/2026-05-30_12-45_adr_pac-enabled-baseline.md` — PAC-enabled baseline that the suite runs against
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — defines the implementer/tester handoff this ADR follows
