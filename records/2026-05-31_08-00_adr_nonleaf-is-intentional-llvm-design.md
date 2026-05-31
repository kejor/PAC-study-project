# ADR: SignReturnAddress::NonLeaf Is Intentional LLVM Design — No Toolchain Patch for t09

**Timestamp**: 2026-05-31 08:00 (UTC-5)
**Type**: ADR
**Author/Source**: User / Agent (architectural analysis)
**Tags**: adr, llvm, aarch64, pac-ret, pac-ret+leaf, leaf-function, SignReturnAddress, NonLeaf, compiler-policy, performance, security-tradeoff, toolchain

## Summary

The question arose after t09 was verified: should `SignReturnAddress::NonLeaf` be patched in the local `arm-toolchain` copy to default to `All` (signing every function, including leaves)? The decision is **No**. `NonLeaf` is an intentional performance/security tradeoff in LLVM's design, not a bug. Android hardened builds use `pac-ret+leaf`; most Linux distributions default to `NonLeaf` for performance. The opt-in fix (`-mbranch-protection=pac-ret+leaf`) already exists in the compiler. Patching the toolchain would misrepresent the real-world baseline and undermine the test's research validity.

## Details

**Decision:** Do not patch `SignReturnAddress::NonLeaf` in the arm-toolchain LLVM source.

**LLVM design rationale (as observed in toolchain source):**

The `SignReturnAddress` enum (`AArch64MachineFunctionInfo.h`) has three values:
- `None` — no PAC instrumentation
- `NonLeaf` — only non-leaf functions get `paciasp`/`autiasp` (the default for `-mbranch-protection=pac-ret`)
- `All` — every function, including leaves, gets `paciasp`/`autiasp` (enabled by `-mbranch-protection=pac-ret+leaf`)

The rationale for `NonLeaf` as default is explicit: leaf functions that do not call other functions never spill LR to the stack in a compiler-managed way. The primary threat PAC-ret is designed to mitigate is stack-based LR corruption. For leaf functions, `X30` is not normally stored to an attacker-accessible stack slot; the attack requires a write primitive directly targeting a register, which is a higher bar. Signing every leaf function adds approximately 2 instructions per function (`paciasp` on entry, `autiasp` before `ret`) — a measurable overhead on hot, short leaf functions in performance-sensitive code.

**Real-world deployment context:**
- **Android hardened builds** (`-android-mbranch-prot=pac-ret+leaf` or equivalent): use `All` — security-critical mobile OS where the overhead is acceptable.
- **Most Linux distributions** (Debian, Ubuntu, Fedora): default to `NonLeaf` — general-purpose OS where performance regressions across the full userspace package set are unacceptable.
- **LLVM upstream position**: `NonLeaf` is the documented default; `pac-ret+leaf` is the documented opt-in hardening flag. This is not a defect report; it is a policy knob.

**Why patching would be wrong for this project:**
1. The test suite's purpose is to characterize the PAC bypass surface as it exists in real-world deployments. Patching `NonLeaf` → `All` as default would make `t09` vacuously pass under the modified toolchain while masking the gap that exists in production systems.
2. The arm-toolchain is used as a reference baseline for the EE202C project; modifying it away from upstream behavior would compromise the scientific validity of the results.
3. The correct fix (`pac-ret+leaf`) already exists as a compiler flag and is demonstrated by the `pac_leaf` variant in t09. The test already documents both the gap and the mitigation.

## Rationale / Decision Basis

- **Performance vs. security tradeoff is documented and intentional.** LLVM's maintainers made a deliberate choice. The project should report this tradeoff, not paper over it.
- **The test suite should reflect real-world compiler behavior.** t09's research value lies precisely in showing that the default `-mbranch-protection=pac-ret` flag leaves leaf functions unprotected — a fact that users and system integrators need to know.
- **Alternatives considered:**
  - Patch `NonLeaf` default to `All` in arm-toolchain: Rejected — masks real-world gap, invalidates research.
  - Add a separate `pac_leaf`-default toolchain build variant: Rejected — overkill; `-mbranch-protection=pac-ret+leaf` is a single compiler flag, not a toolchain rebuild concern.
  - Document the gap and provide the opt-in flag (chosen): Correct approach — this is what t09 already does.

## Impact

- No changes to arm-toolchain source.
- t09 remains a valid demonstration of the leaf-signing gap as it exists in production compiler defaults.
- The project report can cite this as a hardening gap with a known, deployable fix (`-mbranch-protection=pac-ret+leaf`) — a complete and accurate characterization for EE202C.
- This decision establishes the principle for all future test cases: do not patch the toolchain to hide gaps; document and test them as they exist.

## Related Records

- `records/2026-05-31_04-00_adr_leaf-signing-gap-test.md` — ADR-2026-05-30-009, t09 specification
- `records/2026-05-31_07-00_milestone_t09-independent-verification.md` — t09 verification passing
- `records/2026-05-31_09-00_adr_no-toolchain-patches-t01-t09.md` — broader decision on patching any vulnerability in the suite
