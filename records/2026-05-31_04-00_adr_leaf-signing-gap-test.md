# ADR: ADR-2026-05-30-009 — Leaf-Signing Gap Test (t09)

**Timestamp**: 2026-05-31 04:00 (UTC-5)
**Type**: ADR
**Author/Source**: Agent (security-architect)
**Status**: Implemented (commit fa8ca7b)
**Tags**: adr, pac, aarch64, leaf-function, pac-ret, pac-ret+leaf, llvm, compiler-policy, cfi-bypass, test-suite, adr-2026-05-30-009, mbranch-protection, SignReturnAddress

## Summary

ADR-2026-05-30-009 specifies a new test `t09_leaf_signing_gap` to be added to the `tests/pac-vulnerabilities/` suite. The test is grounded in direct analysis of LLVM's AArch64 PAC toolchain source: `AArch64MachineFunctionInfo.h` defines `SignReturnAddress` as `{None, NonLeaf, All}`, and the `-mbranch-protection=pac-ret` flag resolves to `NonLeaf` — meaning leaf functions receive no `paciasp`/`autiasp` instrumentation by default. This is the first test in the suite sourced from direct toolchain source inspection rather than academic papers, and the first test with three build variants (nopac, pac, pac_leaf) where the pac variant PWNs due to a compiler policy default rather than a fundamental PAC limitation.

## Details

**LLVM source basis:**

Two LLVM source files were analyzed directly at `/Users/kejor/ee202c/arm-toolchain/`:

1. `AArch64MachineFunctionInfo.h`:
   - Defines `enum class SignReturnAddress { None, NonLeaf, All }`
   - `-mbranch-protection=pac-ret` maps to `SignReturnAddress::NonLeaf`
   - `-mbranch-protection=pac-ret+leaf` maps to `SignReturnAddress::All`

2. `AArch64PointerAuth.cpp`:
   - `signLR()` and `PAUTH_PROLOGUE`/`PAUTH_EPILOGUE` pseudos are only inserted when `shouldSignReturnAddress()` returns `true`
   - `shouldSignReturnAddress(NonLeaf, IsLeaf=true)` returns `false`
   - Result: leaf functions compiled with `-mbranch-protection=pac-ret` get zero `paciasp`/`autiasp` instructions

**Attack vector:**

A leaf function (one that makes no further calls and thus never saves LR to the stack in a compiler-visible way) still holds the return address in `X30`. An attacker with a write primitive can corrupt `X30` inside a leaf function. On `ret`, the corrupted value is used directly — no `autiasp` is present to validate it. Control jumps to the attacker-controlled address unchecked.

**Three-variant design:**

| Variant | Compile flags | Expected outcome | Rationale |
|---------|---------------|-----------------|-----------|
| nopac   | `-fno-pac-ret` | `PWNED:t09` | Baseline: no PAC at all |
| pac     | `-mbranch-protection=pac-ret` | `PWNED:t09` | The gap: LLVM's NonLeaf default leaves leaf functions unprotected |
| pac_leaf | `-mbranch-protection=pac-ret+leaf` | Blocked (exit 139, SIGSEGV) | Fix: All policy signs every function including leaves |

**Implementation files:**

- `tests/pac-vulnerabilities/t09_leaf_signing_gap/vuln.c`: Contains `leaf_victim()` with inline asm `mov x30, %0` to corrupt X30 directly, followed by `ret`; `exploit_success_t09()` is the attacker-controlled return target
- `tests/pac-vulnerabilities/common/win.h`: Adds `exploit_success_t09()` declaration
- `tests/pac-vulnerabilities/common/win.c`: Adds `exploit_success_t09()` definition (prints `PWNED:t09`)
- `tests/pac-vulnerabilities/Makefile`: Adds `PAC_LEAF_CFLAGS`, per-test `VARIANTS_<test>` / `VARIANTS_default` model (t09 gets 3 variants; t01–t08 keep 2), `ADD_TEST_BINARIES` function, extended `verify-static` with negative assertion (zero `paciasp` in `pac/t09`'s leaf_victim, at least one in `pac_leaf/t09`'s)
- `tests/pac-vulnerabilities/harness/expectations.yml`: Adds t09 3-variant block
- `tests/pac-vulnerabilities/harness/run_tests.py`: Derives variant set per test from `expectations.yml` rather than hardcoded module-level tuple
- `adr/ADR-2026-05-30-009.md`: Full ADR specification (443 lines)

**Total binary count after t09:** 19 (t01–t08 × 2 variants + t09 × 3 variants)

**verify-static extension:**

The `verify-static` Makefile target gained a new assertion specific to t09:
- **Negative assertion:** `pac/t09_leaf_signing_gap` binary contains zero `paciasp` instructions in `leaf_victim` — confirming the gap is present in the compiled output
- **Positive assertion:** `pac_leaf/t09_leaf_signing_gap` binary contains at least one `paciasp` instruction in `leaf_victim` — confirming the fix is applied

These assertions make the compile-time behavior of the toolchain gap machine-verifiable, not just runtime-observable.

## Rationale / Decision Basis

- **Toolchain-sourced vulnerability:** Prior tests (t06–t08) are grounded in academic papers. t09 is the first test whose root cause was identified by reading LLVM source code directly. The `SignReturnAddress::NonLeaf` enum value is the precise compiler mechanism responsible; documenting it at this level of specificity strengthens the project's technical credibility.
- **Compiler policy gap vs. fundamental PAC limitation:** t06, t07, t08 demonstrate weaknesses in PAC's threat model or attack-surface scope. t09 demonstrates that even within the threat model PAC is supposed to cover (return address integrity), the default compiler policy leaves a class of functions unprotected. The distinction matters for the EE202C report's "characterization of bypass surface."
- **Three-variant structure documents both the gap and the fix:** By including `pac_leaf` as a third variant with a blocked outcome, the test shows that the gap is an implementation/policy choice, not an architectural necessity. `-mbranch-protection=pac-ret+leaf` closes it. This frames the attack correctly: it is a hardening gap, not a hardware weakness.
- **Makefile per-test variant model:** Prior tests assumed exactly two variants (nopac, pac). The new `VARIANTS_<test>` / `VARIANTS_default` pattern allows future tests to declare different variant counts without changing harness code, making the suite extensible for subsequent ADRs.

## Impact

- Extends `tests/pac-vulnerabilities/` to 19 test binaries across 9 test cases (t01–t09).
- Establishes toolchain source analysis as a valid, first-class method for identifying new test cases in this suite — not just academic paper adaptation.
- The `run_tests.py` harness change (variant set per-test from `expectations.yml`) is a correctness improvement that removes the prior implicit assumption of a fixed 2-variant structure.
- `verify-static` now includes a compile-time correctness check for the toolchain gap, making the gap machine-verifiable at build time.
- The pac_leaf variant establishes `-mbranch-protection=pac-ret+leaf` as a tested, confirmed mitigation — relevant to the EE202C report's mitigations section.
- Provides a concrete, citable example for the report: the default `pac-ret` flag is insufficient for leaf functions; hardened deployments must use `pac-ret+leaf`.

## Related Records

- `records/2026-05-31_05-00_milestone_t09-leaf-signing-gap-implemented.md` — implementation milestone (commit fa8ca7b)
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, parent test suite spec
- `records/2026-05-30_20-15_adr_pac-oracle-bruteforce-test.md` — ADR-2026-05-30-008, immediately preceding ADR
- `records/2026-05-31_03-00_milestone_t08-pac-oracle-bruteforce-independent-verification.md` — prior suite state (18/18 passing) before t09 was added
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — implementer/tester handoff pipeline
