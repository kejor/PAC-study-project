# Milestone: ADR-2026-05-30-009 Leaf-Signing Gap Test (t09) — Implemented (commit fa8ca7b)

**Timestamp**: 2026-05-31 05:00 (UTC-5)
**Type**: Milestone
**Author/Source**: Agent (adr-implementer)
**Tags**: milestone, pac, aarch64, leaf-function, pac-ret, pac-ret+leaf, llvm, compiler-policy, cfi-bypass, vulnerability-testing, test-suite, adr-2026-05-30-009, mbranch-protection, SignReturnAddress, makefile-refactor, harness-refactor

## Summary

ADR-2026-05-30-009 was implemented in full at commit `fa8ca7b`. The implementation adds `t09_leaf_signing_gap` — a 3-variant test (nopac, pac, pac_leaf) demonstrating that LLVM's `-mbranch-protection=pac-ret` default (`SignReturnAddress::NonLeaf`) leaves leaf functions without `paciasp`/`autiasp` instrumentation, while `-mbranch-protection=pac-ret+leaf` (`SignReturnAddress::All`) closes the gap. This is the first 3-variant test in the suite, the first where the pac variant is expected to PWN due to a compiler policy default, and the first test grounded in direct LLVM toolchain source analysis rather than academic papers.

## Details

**Commit:** `fa8ca7b` — `feat: implement ADR-2026-05-30-009 — t09 leaf-signing gap test`
**Author:** Kevin Oviedo (`kevinoviedorueda@gmail.com`)
**Date:** 2026-05-31

**Files changed (8 files, +680 / -33):**

| File | Change |
|------|--------|
| `adr/ADR-2026-05-30-009.md` | New — full 443-line ADR specification |
| `tests/pac-vulnerabilities/t09_leaf_signing_gap/vuln.c` | New — 108-line test; inline asm `mov x30, %0` to corrupt X30 in `leaf_victim()` |
| `tests/pac-vulnerabilities/common/win.h` | Modified — adds `exploit_success_t09()` declaration |
| `tests/pac-vulnerabilities/common/win.c` | Modified — adds `exploit_success_t09()` definition (prints `PWNED:t09`, exits 0) |
| `tests/pac-vulnerabilities/Makefile` | Modified (+97 lines) — `PAC_LEAF_CFLAGS`, `VARIANTS_<test>` / `VARIANTS_default` per-test model, `ADD_TEST_BINARIES` function, `verify-static` negative/positive paciasp assertions for t09 |
| `tests/pac-vulnerabilities/harness/expectations.yml` | Modified (+16 lines) — t09 3-variant block (nopac: PWNED, pac: PWNED, pac_leaf: blocked) |
| `tests/pac-vulnerabilities/harness/run_tests.py` | Modified (+13 lines) — derives variant set per test from `expectations.yml` keys, not hardcoded tuple |
| `tests/pac-vulnerabilities/README.md` | Modified (+34 lines) — t09 entry, updated test count, leaf-signing gap explanation |

**Key implementation details:**

`vuln.c` — `leaf_victim()` function:
- Declared `__attribute__((noinline))` to prevent inlining (which would remove leaf status)
- Takes a `uint64_t target` argument (the attacker's redirect address)
- Uses inline asm `"mov x30, %0"` to overwrite X30 with the target before executing `ret`
- Under nopac and pac, X30 is used directly on `ret` — no auth check; control reaches `exploit_success_t09()`
- Under pac_leaf, `autiasp` is present; the unauthenticated raw address in X30 has its error bit set by the failed auth and faults on `ret`

Makefile changes:
- `PAC_LEAF_CFLAGS := $(PAC_CFLAGS:-mbranch-protection=pac-ret=-mbranch-protection=pac-ret+leaf)` — derives pac_leaf flags from pac flags via substitution
- `VARIANTS_t09_leaf_signing_gap := nopac pac pac_leaf` overrides the `VARIANTS_default := nopac pac` for this test
- `ADD_TEST_BINARIES` function iterates `VARIANTS_<testname>` (with fallback to `VARIANTS_default`) and adds each variant to `TEST_BINARIES`
- `verify-static` additions: `objdump | grep paciasp` with count checks per variant of t09

`run_tests.py` changes:
- Replaces hardcoded `VARIANTS = ('nopac', 'pac')` module constant with per-test variant extraction from `expectations.yml` keys
- Each test in the expectations file now dictates its own variant list — the harness is fully driven by `expectations.yml`

**New test count:** 19 binaries total (t01–t08 × 2 variants = 16; t09 × 3 variants = 3)

**Expected results per variant:**

| Variant | Expected | Root cause |
|---------|----------|------------|
| nopac | `PWNED:t09`, exit 0 | No PAC at all; raw X30 corruption succeeds trivially |
| pac | `PWNED:t09`, exit 0 | `SignReturnAddress::NonLeaf` — leaf functions not signed under `-mbranch-protection=pac-ret` |
| pac_leaf | exit 139 (SIGSEGV) | `SignReturnAddress::All` — `autiasp` inserted; corrupted X30 faults on auth failure |

## Impact

- The suite now covers 9 distinct attack/defense classes across 19 binaries.
- The Makefile's `VARIANTS_<test>` model makes the suite future-proof: any subsequent ADR can declare a different variant count without Makefile restructuring.
- The `run_tests.py` per-test variant extraction eliminates a latent correctness risk: prior code assumed every test had exactly (nopac, pac) — t09 would have silently omitted the pac_leaf variant under the old logic.
- `verify-static` now asserts both the presence of the gap (zero `paciasp` in pac/t09) and the fix (at least one `paciasp` in pac_leaf/t09) at build time, making the toolchain behavior machine-verifiable.
- First test grounded in direct LLVM source reading: establishes toolchain source analysis as a first-class research method for the EE202C project.
- First test where the pac variant is expected to fail (PWNED) due to compiler default policy, not a fundamental PAC architectural weakness — a qualitatively distinct data point for the project report's bypass surface characterization.

## Related Records

- `records/2026-05-31_04-00_adr_leaf-signing-gap-test.md` — ADR-2026-05-30-009 specification
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, parent test suite spec
- `records/2026-05-31_03-00_milestone_t08-pac-oracle-bruteforce-independent-verification.md` — prior suite state (18/18 passing) before t09 was added
- `records/2026-05-30_20-15_adr_pac-oracle-bruteforce-test.md` — ADR-2026-05-30-008, immediately preceding ADR
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — implementer/tester handoff pipeline
