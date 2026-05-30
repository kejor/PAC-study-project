# ADR: ADR-2026-05-30-006 — SP-Collision PAC Pointer Reuse Test (t06)

**Timestamp**: 2026-05-30 18:00 (UTC-5)
**Type**: ADR
**Author/Source**: Agent (security-architect)
**Status**: Ready (sent to adr-implementer)
**Tags**: adr, pac, aarch64, sp-collision, pac-reuse, parts, pointer-reuse, rop, cfi-bypass, test-suite, usenix-security-19

## Summary

ADR-2026-05-30-006 specifies a new test `t06_sp_collision_pac_reuse` to be added to the `tests/pac-vulnerabilities/` suite. The test is grounded in the SP-collision pointer-reuse weakness documented in Section 3 and Section 7.2.1 of Liljestrand et al., "PAC it up: Towards Pointer Integrity Using ARM Pointer Authentication" (USENIX Security '19). This is the first test in the suite where the PAC-enabled variant is also expected to output the PWNED sentinel — it demonstrates a PAC bypass, not a PAC defense, and distinguishes the baseline `-mbranch-protection=pac-ret` scheme from the full PARTS defense.

## Details

**Academic basis:**
Liljestrand et al. (USENIX Security '19), Section 3 ("Background and Threat Model") and Section 7.2.1 ("Pointer Reuse Attacks"). GCC's `-mbranch-protection=pac-ret` signs the saved LR with `paciasp`, which uses a context modifier derived solely from the current SP value. This means two distinct functions at the same stack depth share the same SP modifier, so a valid PAC-signed return address captured from a donor function can be transplanted verbatim into the saved LR slot of a victim function. `autiasp` in the victim will authenticate successfully because the SP modifier matches. PARTS addresses this by XOR-ing a compile-time function-identifier (derived from the function's address) into the modifier, making PAC tags function-specific even at equal stack depths.

**Attack scenario:**

1. A donor function `donor()` is called at a known stack depth; its `paciasp`-signed LR is captured (e.g., via an arbitrary read, use-after-free disclosure, or stack disclosure primitive).
2. A victim function `victim()` is called at the same stack depth; its saved LR slot is overwritten with the donor's signed LR via a stack write primitive.
3. On return from `victim()`, `autiasp` verifies the transplanted PAC tag — it passes because SP is identical.
4. Control is redirected to wherever the donor's LR pointed (i.e., the attacker's chosen target).

**Expected outcomes:**

| Variant | Expected outcome |
|---------|-----------------|
| PAC enabled (`-mbranch-protection=pac-ret`) | `PWNED:t06` printed — bypass succeeds |
| PAC disabled (`-fno-pac-ret`) | `PWNED:t06` printed — bypass succeeds trivially |

Both variants are expected to succeed. This is intentional and is the key distinguishing property of t06 relative to t01–t04.

**Planned test layout:**

```
tests/pac-vulnerabilities/t06/
  Makefile        — builds donor/victim harness under both PAC-on and PAC-off
  target.c        — defines donor(), victim(), and a controlled-write primitive
  exploit.c       — captures donor PAC-signed LR, transplants into victim's saved LR
  README.md       — explains the SP-collision weakness and expected results
```

**`expectations.yml` addition:**

```yaml
t06:
  pac_enforced:
    sentinel_present: "PWNED:t06"   # PAC-ret does NOT protect: SP-collision bypass
  pac_disabled:
    sentinel_present: "PWNED:t06"
```

**Key implementation constraints:**

- Both `donor()` and `victim()` must be called at the same SP value. The harness must guarantee this — caller must set up identical stack frames for both invocations.
- `-fno-stack-protector` is mandatory (consistent with t01–t05) to prevent SSP from preempting the PAC/no-PAC signal.
- The PAC-signed LR disclosure primitive must work at the C level using `__builtin_frame_address(0)` arithmetic to locate the saved LR; no hardcoded offsets.
- The write primitive must overwrite only the saved LR of the victim without corrupting adjacent frame data, so that `autiasp` is reached cleanly.
- The `PWNED:t06` sentinel is written by the attacker-controlled return target, consistent with suite-wide sentinel convention.

## Rationale / Decision Basis

- **Grounded in peer-reviewed literature:** The weakness is precisely characterized in Liljestrand et al. §3 and §7.2.1, giving the test a citable, well-bounded specification. This is directly relevant to EE202C Project A's deliverable of characterizing PAC's bypass surface.
- **First bypass test in the suite:** t01–t04 demonstrate PAC defenses (expected PAC signal: auth failure). t05 demonstrates PAC non-coverage of non-LR data. t06 is the first test where PAC-ret is actively bypassed on a return address — a qualitatively different result that requires an explicit entry in `expectations.yml` to avoid being misread as a harness failure.
- **Distinguishes pac-ret from PARTS:** The weakness exploited by t06 is exactly what PARTS closes by adding a function-id to the modifier. Having a runnable test that fails under `-mbranch-protection=pac-ret` and would pass under PARTS creates a concrete, executable comparison point for the project report and presentation.
- **Consistent with suite conventions:** PWNED sentinel format, Python harness, `expectations.yml` schema, `-fno-stack-protector`, and hermetic payload construction using frame-address intrinsics are all inherited from ADR-2026-05-30-005.

## Impact

- Extends `tests/pac-vulnerabilities/` with a pointer-reuse attack class not covered by t01–t05.
- Provides a citable, runnable demonstration of the SP-collision weakness for the EE202C Project A report and final presentation.
- The `expectations.yml` entry for t06 requires the harness to handle the case where PAC-enabled is expected to print a PWNED sentinel — the harness logic must not treat a PAC-on PWNED as an automatic FAIL.
- Establishes a baseline against which a hypothetical PARTS-style (function-id modifier) implementation could be benchmarked in a future extension.
- adr-implementer is responsible for scaffolding `tests/pac-vulnerabilities/t06/` and updating `expectations.yml`. adr-implementation-tester must verify both PAC-on and PAC-off variants print `PWNED:t06` before the test is considered done.

## Related Records

- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, the parent test suite spec this test extends
- `records/2026-05-30_16-30_milestone_pac-vulnerability-test-suite-implemented.md` — milestone confirming t01–t05 are implemented
- `records/2026-05-30_17-00_milestone_pac-test-suite-independent-verification.md` — independent verification of t01–t05
- `records/2026-05-30_12-45_adr_pac-enabled-baseline.md` — PAC-enabled baseline that the suite runs against
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — defines the implementer/tester handoff this ADR follows
