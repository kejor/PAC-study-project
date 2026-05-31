# Milestone: ADR-2026-05-30-009 t09 Leaf-Signing Gap — Independent Verification Passing (21/21)

**Timestamp**: 2026-05-31 07:00 (UTC-5)
**Type**: Milestone
**Author/Source**: Agent (adr-implementation-tester)
**Tags**: milestone, pac, aarch64, t09, leaf-function, pac-ret, pac-ret+leaf, verification, test-suite, adr-2026-05-30-009, naked-function

## Summary

After the naked-function fix at commit `c4d1301`, the full PAC vulnerability test suite reached 21/21 passing cases. ADR-2026-05-30-009 is fully implemented and independently verified: `make verify-static` confirms the leaf-signing gap at the binary level, and `make test` confirms the correct runtime behavior across all three t09 variants (nopac, pac, pac_leaf). The project's PAC vulnerability test suite is complete with 9 distinct test cases spanning all identified PAC bypass surface categories.

## Details

**Verification commit:** `c4d1301` (the fix) — all 21 cases pass after this commit.

**Suite state at completion:**

| Test | Variant | Expected | Result |
|------|---------|----------|--------|
| t01 | nopac | PWNED:t01 | PASS |
| t01 | pac | trap (exit 135) | PASS |
| t02 | nopac | PWNED:t02 | PASS |
| t02 | pac | trap (exit 135) | PASS |
| t03 | nopac | PWNED:t03 | PASS |
| t03 | pac | trap (exit 135) | PASS |
| t04 | nopac | PWNED:t04 | PASS |
| t04 | pac | trap (exit 135) | PASS |
| t05a | nopac | PWNED:t05a | PASS |
| t05a | pac | PWNED:t05a | PASS |
| t05b | nopac | PWNED:t05b | PASS |
| t05b | pac | PWNED:t05b | PASS |
| t06 | nopac | PWNED:t06 | PASS |
| t06 | pac | PWNED:t06 | PASS |
| t07 | nopac | PWNED:t07 | PASS |
| t07 | pac | PWNED:t07 | PASS |
| t08 | nopac | PWNED:t08 | PASS |
| t08 | pac | PWNED:t08 | PASS |
| t09 | nopac | PWNED:t09 | PASS |
| t09 | pac | PWNED:t09 (leaf gap) | PASS |
| t09 | pac_leaf | trap (exit 135) | PASS |

**verify-static specific assertions for t09:**
- `pac/t09_leaf_signing_gap` disassembly of `leaf_victim`: 0 `paciasp` instructions — gap confirmed
- `pac_leaf/t09_leaf_signing_gap` disassembly of `leaf_victim`: 1 `paciasp` instruction — fix confirmed

**Total binaries:** 21 across 9 test cases

## Impact

The PAC vulnerability test suite is complete and fully verified. All 9 ADRs in the series (ADR-2026-05-30-001 through ADR-2026-05-30-009) have been implemented and independently verified. The suite now constitutes a comprehensive, machine-verifiable characterization of the PAC bypass surface relevant to the EE202C IoT security class project.

The full suite covers:
- **t01–t04, t07**: Cases where PAC correctly blocks return-address corruption (establishes the baseline threat model that PAC is designed to address)
- **t05a/t05b**: Forward-edge and data-only attacks outside pac-ret's threat model
- **t06**: SP-collision PAC reuse (architectural modifier limitation)
- **t08**: PAC oracle brute-force (entropy-class weakness)
- **t09**: Leaf-signing gap (compiler policy default under `-mbranch-protection=pac-ret`)

## Related Records

- `records/2026-05-31_06-00_bug-fix_t09-naked-function-gcc-lr-clobber.md` — fix that enabled t09 to pass
- `records/2026-05-31_05-00_milestone_t09-leaf-signing-gap-implemented.md` — initial t09 implementation
- `records/2026-05-31_04-00_adr_leaf-signing-gap-test.md` — ADR-2026-05-30-009 specification
- `records/2026-05-31_03-00_milestone_t08-pac-oracle-bruteforce-independent-verification.md` — prior suite state (t01–t08, 18/18)
