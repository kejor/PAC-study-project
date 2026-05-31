# ADR: ADR-2026-05-30-007 — Explicit PAC Data Signing Test (t07)

**Timestamp**: 2026-05-30 20:00 (UTC-5)
**Type**: ADR
**Author/Source**: Agent (security-architect)
**Status**: Ready (sent to adr-implementer)
**Tags**: adr, pac, aarch64, data-integrity, pacia, autia, inline-asm, explicit-signing, positive-control, cfi-bypass, test-suite, ictc-2025

## Summary

ADR-2026-05-30-007 specifies a new test `t07_explicit_data_signing` to be added to the `tests/pac-vulnerabilities/` suite. The test is grounded in Choi et al., "High-Performance and Secure Data Integrity Verification using ARM Pointer Authentication Code" (ICTC 2025), which demonstrates that `pacia`/`autia` inline assembly can explicitly sign and authenticate arbitrary data values — not just return addresses. This test forms the positive complement to t05b (data-only attack, PAC non-coverage): where t05b shows that `-mbranch-protection=pac-ret` alone cannot protect data, t07 shows that explicit `pacia`/`autia` inline asm CAN. The build is controlled via `-DUSE_PAC_DATA_SIGNING` (not `-mbranch-protection`) because inline asm is not subject to compiler flag gating.

## Details

**Academic basis:**
Choi et al., "High-Performance and Secure Data Integrity Verification using ARM Pointer Authentication Code," ICTC 2025. The paper demonstrates applying `pacia`/`autia` (instruction-key PAC sign/auth) to protect arbitrary in-memory data values, not only the link register. This is architecturally valid: `pacia` takes a 64-bit value and a 64-bit modifier and produces a PAC-tagged value; `autia` verifies the tag and strips it (or traps on mismatch). The technique is independent of `-mbranch-protection`, which only instruments function prologues/epilogues.

**Attack scenario and test structure:**

The target program stores a security-critical data value (e.g., a privilege flag or an integrity-checked counter). The exploit attempts to overwrite that value with an attacker-controlled one:

- **PAC-disabled (`-DUSE_PAC_DATA_SIGNING` absent):** No signing or verification occurs. The overwrite is undetected and the program prints `PWNED:t07`.
- **PAC-enabled (`-DUSE_PAC_DATA_SIGNING` defined):** The data value is signed with `pacia` at write time. On read, `autia` authenticates the tag. The corrupted value fails authentication, raising a hardware exception (SIGILL/SIGSEGV on Apple Silicon; SIGBUS on Linux/QEMU). `PWNED:t07` is never printed.

**Expected outcomes:**

| Variant | Build flag | Expected outcome |
|---------|-----------|-----------------|
| PAC disabled | (no flag) | `PWNED:t07` printed |
| PAC enabled (explicit signing) | `-DUSE_PAC_DATA_SIGNING` | PAC auth failure (SIGILL/SIGSEGV/SIGBUS) — `PWNED:t07` absent |

**Planned test layout:**

```
tests/pac-vulnerabilities/t07/
  Makefile        — builds under both -DUSE_PAC_DATA_SIGNING and default (no flag)
  target.c        — stores data value using pacia; verifies with autia on read
  exploit.c       — overwrites the signed data value in memory
  README.md       — explains the Choi et al. technique and its relation to t05b
```

**`expectations.yml` addition:**

```yaml
t07:
  pac_enforced:
    exit_signals: [SIGBUS, SIGILL, SIGSEGV]
    sentinel_absent: "PWNED:t07"
  pac_disabled:
    sentinel_present: "PWNED:t07"
```

**Key implementation constraints:**

- The `pacia`/`autia` instructions must be issued via `__asm__ volatile` inline assembly, not compiler builtins; `-mbranch-protection` does not control this path.
- The build matrix uses `-DUSE_PAC_DATA_SIGNING` as the toggle, keeping the same source file compilable in both modes via `#ifdef`.
- `-fno-stack-protector` is mandatory (consistent with t01–t06) to isolate the PAC signal.
- The `PWNED:t07` sentinel is written by the exploit-controlled code path on successful data overwrite, consistent with suite-wide sentinel convention.
- ADR-007 must land and be implemented before ADR-008 (t08), because t08's `Makefile` references a `BUILD_RULE` target or shared infrastructure introduced by t07.

## Rationale / Decision Basis

- **Closes the gap left by t05b:** t05b (data-only attack, negative control) establishes that `-mbranch-protection=pac-ret` cannot protect arbitrary data. t07 demonstrates the affirmative claim from Choi et al.: explicit `pacia`/`autia` inline asm CAN protect data. Having both tests makes PAC's coverage envelope precisely executable and citable.
- **Preprocessor flag, not compiler flag:** Inline asm instructions are not gated by `-mbranch-protection`. Using `-DUSE_PAC_DATA_SIGNING` as the toggle accurately reflects the implementation reality and avoids misleading readers into thinking the protection is automatic under the standard build flags.
- **Grounded in 2025 peer-reviewed literature:** Citing Choi et al. (ICTC 2025) as the academic basis gives the test a concrete, citable specification and connects the EE202C project deliverable to current research on PAC-based data integrity.
- **Consistent with suite conventions:** PWNED sentinel format, Python harness, `expectations.yml` schema, `-fno-stack-protector`, and hermetic payload construction are all inherited from ADR-2026-05-30-005.

## Impact

- Extends `tests/pac-vulnerabilities/` with a PAC data-integrity positive-control test, complementing t05b's negative control.
- Provides a citable, runnable demonstration that explicit `pacia`/`autia` signing defeats data-only attacks for the EE202C Project A report and final presentation.
- Must be implemented before t08 (ADR-2026-05-30-008) due to Makefile `BUILD_RULE` dependency.
- adr-implementer is responsible for scaffolding `tests/pac-vulnerabilities/t07/` and updating `expectations.yml`. adr-implementation-tester must verify the PAC-enabled variant traps and the PAC-disabled variant prints `PWNED:t07` before the test is considered done.

## Related Records

- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, the parent test suite spec this test extends
- `records/2026-05-30_16-30_milestone_pac-vulnerability-test-suite-implemented.md` — milestone confirming t01–t05 are implemented
- `records/2026-05-30_17-00_milestone_pac-test-suite-independent-verification.md` — independent verification of t01–t05
- `records/2026-05-30_18-00_adr_sp-collision-pac-reuse-test.md` — ADR-2026-05-30-006 (t06), the immediately preceding test in the suite
- `records/2026-05-30_12-45_adr_pac-enabled-baseline.md` — PAC-enabled baseline that the suite runs against
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — defines the implementer/tester handoff this ADR follows
