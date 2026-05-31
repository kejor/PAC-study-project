# ADR: No PAC Vulnerability in the Test Suite (t01–t09) Should Be Patched in the Toolchain

**Timestamp**: 2026-05-31 09:00 (UTC-5)
**Type**: ADR
**Author/Source**: User / Agent (architectural analysis)
**Tags**: adr, pac, aarch64, toolchain, test-suite, threat-model, bypass-surface, t01-t09, compiler-policy, architectural-limitation, pac-ret, cfb-metrics, ee202c

## Summary

A broader architectural question was posed after the t09 toolchain-patch debate: should any of the PAC vulnerabilities demonstrated in t01–t09 be patched in the arm-toolchain to "fix" them? The decision is **No** for all nine test cases. Each test falls into one of three definitive categories that preclude a toolchain patch: (A) tests where PAC works correctly and demonstrates intended behavior, (B) tests that are outside pac-ret's stated threat model entirely, and (C) tests where the root cause is a known architectural limitation requiring hardware beyond AArch64v8.5 or a specific opt-in hardening flag that already exists. Patching the toolchain for any of these would corrupt the research baseline and misrepresent the real-world PAC bypass surface.

## Details

**Decision:** No toolchain patches for any test case t01–t09.

**Full classification:**

### Category A: PAC Works Correctly — Toolchain Is Doing Its Job

These tests demonstrate that PAC-ret successfully detects and traps return-address corruption when the threat model applies. The toolchain behavior is correct by design.

| Test | Vulnerability class | PAC outcome | Why no patch needed |
|------|-------------------|-------------|---------------------|
| t01 | Basic stack LR overwrite | Blocks (SIGBUS/exit 135) | PAC works as designed. The nopac variant shows what happens without it. |
| t02 | ROP chain via stack corruption | Blocks (SIGBUS/exit 135) | PAC works as designed. |
| t03 | Buffer overflow into stack frame containing LR | Blocks (SIGBUS/exit 135) | PAC works as designed. |
| t04 | Heap-spray into stack-allocated function pointer | Blocks (SIGBUS/exit 135) | PAC works as designed. |
| t07 | Explicit PAC signing of data pointers (`pacia`, `autia`) | Blocks for data pointer; tests signing API | PAC signing API works correctly; correct usage blocks forgery. |

For t01–t04 and t07, the PAC variant blocks the exploit. These are not vulnerabilities in PAC; they are the baseline demonstrating what PAC is supposed to protect. No patch is needed because there is nothing to fix.

### Category B: Outside pac-ret's Stated Threat Model

These tests demonstrate attacks that `-mbranch-protection=pac-ret` explicitly does not claim to prevent. Patching the toolchain to address them would require implementing a different security mechanism entirely — not a fix to existing functionality.

| Test | Attack class | Why outside threat model |
|------|-------------|--------------------------|
| t05a | Forward-edge control-flow hijack (function pointer corruption) | pac-ret protects return addresses only. Forward-edge CFI (e.g., `-fpac-ret` does not sign indirect call targets) is a separate mechanism. Addressing this requires `-mbranch-protection=bti` (Branch Target Identification) or compiler-based forward-edge CFI schemes. |
| t05b | Data-only attack (corrupting a security-critical data variable, not a code pointer) | PAC has no mechanism for data-only attacks. This is outside the scope of pointer authentication as a hardware primitive. Defenses require orthogonal techniques (memory tagging, MPK, etc.). |

### Category C: Architectural Limitation — Fix Requires ARMv8.6+ Hardware or an Existing Opt-In Flag

These tests expose real weaknesses, but the correct fix is either: (a) a specific opt-in compiler flag that already exists, or (b) hardware features beyond the ARMv8.5-A baseline used by this project.

| Test | Root cause | Required fix | Status |
|------|-----------|-------------|--------|
| t06 | SP-collision PAC reuse (same SP value → same PAC → forgeable) | PAuth2 / PAuthLR (ARMv8.6+): uses instruction address as an additional modifier, making context-specific PAC values unique even for the same SP | Supported in LLVM via `BranchProtectionPAuthLR` but requires ARMv8.6+ hardware; not available in the QEMU ARMv8.5 baseline. Not a toolchain bug. |
| t08 | PAC entropy brute-force (48-bit VA → 7-bit PAC → 128 guesses max) | FPAC (Fault on PAC failure, ARMv8.6+): converts AUT* failure from silent pointer corruption to a hardware exception, preventing iterative oracle use | Requires ARMv8.6+ hardware. FPAC is a hardware feature, not a compiler flag. Not a toolchain bug. |
| t09 | Leaf-signing gap (`-mbranch-protection=pac-ret` defaults to `NonLeaf`) | `-mbranch-protection=pac-ret+leaf` (`SignReturnAddress::All`): already exists as an opt-in LLVM flag | Fix exists and is demonstrated by t09's `pac_leaf` variant. Patching `NonLeaf` as default would misrepresent real-world deployments. Not a toolchain bug. |

## Rationale / Decision Basis

**The test suite's purpose is descriptive, not prescriptive.** The goal is to characterize the PAC bypass surface as it exists in real-world AArch64 deployments compiled with `-mbranch-protection=pac-ret`. Patching the toolchain to eliminate the vulnerabilities would make the suite measure an imaginary hardened baseline that does not exist in practice.

**Each category has a principled reason, not just a policy preference:**
- Category A: Nothing to patch — PAC works.
- Category B: Wrong tool for the job — these attacks require different defenses.
- Category C: The fixes exist (either as opt-in flags or ARMv8.6+ hardware); the test suite's job is to document where they are needed, not to simulate their presence.

**Consistency principle:** If even one test were patched, the suite would lose internal consistency. A reader comparing t06 (unpatched) and a hypothetical patched t09 would be confused about whether the suite reports real or idealized behavior.

**Alternatives considered:**
- Patch individual tests with their corresponding fixes in the toolchain: Rejected — corrupts baseline.
- Create a second "hardened" toolchain variant that applies all known fixes: Possible in theory, but out of scope for EE202C; the project is about characterizing bypass surface, not hardening deployments.
- Leave everything as-is and document the fix for each category in the report (chosen): Correct approach.

## Impact

- No changes to arm-toolchain source for any test case t01–t09.
- The three-category framework (PAC works / outside threat model / architectural limitation) provides a clean taxonomy for the EE202C project report's bypass surface characterization section.
- Future test cases (if any are added) should be classified into this same taxonomy before being added to the suite — this prevents scope creep into "tests that require patching the toolchain to be meaningful."
- Establishes the research integrity principle for this project: the toolchain is a fixed, upstream-representative artifact; test results are reported against it as-is.

## Related Records

- `records/2026-05-31_08-00_adr_nonleaf-is-intentional-llvm-design.md` — specific t09 / NonLeaf discussion that preceded this broader ADR
- `records/2026-05-30_18-00_adr_sp-collision-pac-reuse-test.md` — ADR-2026-05-30-006, t06 specification (SP collision, PAuth2 limitation)
- `records/2026-05-30_20-15_adr_pac-oracle-bruteforce-test.md` — ADR-2026-05-30-008, t08 specification (entropy / FPAC)
- `records/2026-05-31_04-00_adr_leaf-signing-gap-test.md` — ADR-2026-05-30-009, t09 specification
- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005, parent test suite specification
