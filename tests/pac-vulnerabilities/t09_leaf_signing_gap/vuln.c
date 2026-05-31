/*
 * t09_leaf_signing_gap/vuln.c -- ADR-2026-05-30-009.
 *
 * Primitive: LEAF FUNCTION X30 CORRUPTION under the default pac-ret
 *   policy. LLVM's AArch64 backend defines
 *
 *     enum class SignReturnAddress { None, NonLeaf, All };
 *
 *   in arm-toolchain/llvm/lib/Target/AArch64/AArch64MachineFunctionInfo.h.
 *   The default for `-mbranch-protection=pac-ret` is
 *   SignReturnAddress::NonLeaf. The helper
 *
 *     bool shouldSignReturnAddress(SignReturnAddress Cond, bool IsLeaf)
 *
 *   returns false when Cond == NonLeaf && IsLeaf == true. In
 *   arm-toolchain/llvm/lib/Target/AArch64/AArch64PointerAuth.cpp, signLR()
 *   and the PAUTH_PROLOGUE / PAUTH_EPILOGUE pseudos are only inserted when
 *   shouldSignReturnAddress() returns true. Consequence: a leaf function
 *   compiled under pac-ret contains NO paciasp in its prologue and NO
 *   autiasp in its epilogue. Its `ret` is therefore an unauthenticated
 *   indirect branch on whatever value lives in X30 at the point of return.
 *
 *   GCC's aarch64 backend implements the same policy: under
 *   -mbranch-protection=pac-ret, a function that does not save LR to the
 *   stack (i.e. a true leaf) receives no paciasp/autiasp. The empirical
 *   asm dump under this toolchain (aarch64-unknown-linux-gnu-gcc 15.2.0)
 *   confirms the equivalence -- see Makefile verify-static.
 *
 *   The mitigation is -mbranch-protection=pac-ret+leaf, which selects
 *   SignReturnAddress::All. Under All, shouldSignReturnAddress() returns
 *   true for leaf and non-leaf functions alike, so paciasp / autiasp are
 *   emitted everywhere. This is documented in the ARM ACLE and is the
 *   configuration shipped by some downstream distributions (e.g.
 *   Android's hardened builds) precisely to close this gap.
 *
 * Why a __attribute__((naked)) function with an inline-asm body rather
 *   than an ordinary C function with a `mov x30, %0 : ::: "x30"` clobber.
 *   Listing x30 in an inline-asm clobber list inside an ordinary C
 *   function causes GCC to treat the function as needing to preserve LR
 *   across the asm: GCC therefore emits a frame that spills x30 to the
 *   stack and reloads it before `ret`. Two consequences follow that
 *   defeat the test:
 *     (1) The reload overwrites the corrupted x30 with the originally
 *         saved LR -- the corruption never reaches `ret`.
 *     (2) The frame setup makes the function non-leaf from the
 *         compiler's pac-ret point of view, so paciasp IS emitted even
 *         under -mbranch-protection=pac-ret, erasing the gap this test
 *         exists to surface.
 *   A naked function lets us write the body in pure assembly with no
 *   compiler-inserted frame and no x30 spill. The compiler still
 *   emits the function's own prologue/epilogue around our body when
 *   building under pac_leaf (because SignReturnAddress::All forces
 *   paciasp/autiasp on every function), but it emits NOTHING extra
 *   under pac-ret (because the function makes no calls and saves no
 *   LR -- the leaf-signing gap, statically visible).
 *
 *   GCC currently warns that the naked attribute is "ignored" on
 *   aarch64. Empirically that warning is misleading on GCC 15.2: the
 *   compiler still suppresses the C-level frame setup, and the
 *   resulting code matches the intended three-variant disassembly
 *   (verified by the Makefile's verify-static target).
 *
 * Why both nopac AND pac PWN. This is the load-bearing observation of
 *   this test. Under nopac there is no PAC at all, so the corrupted X30
 *   is consumed by `ret` unchecked. Under pac (-mbranch-protection=
 *   pac-ret, default) the leaf function's prologue/epilogue contain
 *   no paciasp/autiasp, so again `ret` consumes the corrupted X30
 *   unchecked. The pac variant succeeding demonstrates that the
 *   default pac-ret policy leaves leaf functions unsigned. Under
 *   pac_leaf (-mbranch-protection=pac-ret+leaf) the epilogue is
 *   `autiasp; ret`; autiasp poisons the corrupted X30 and the
 *   subsequent ret faults (SIGSEGV / SIGBUS / SIGILL).
 *
 *   This is the third entry in the suite where the pac variant is
 *   expected PWNED (t06 first, t08 second), and the FIRST entry where
 *   the expected outcome depends on a finer-grained PAC policy
 *   (pac-ret vs pac-ret+leaf) rather than on the presence/absence of
 *   pac-ret altogether.
 *
 * Hardware caveat (same as t07, t08). The pac_leaf variant assumes real
 *   ARMv8.3 PAC. On a host without it, autiasp decodes as a HINT NOP and
 *   the test would PWN under pac_leaf for the wrong reason (no real
 *   authentication happens, so the corrupted X30 reaches `ret`). The
 *   project runs on Apple Silicon via Docker linux/arm64, where real
 *   PAC is exposed via the Apple Virtualization Framework, so this is
 *   a documented caveat rather than a blocker. The Makefile's
 *   verify-static target additionally asserts at the static level that
 *   paciasp IS emitted in pac_leaf/t09 and is NOT emitted in pac/t09,
 *   giving a toolchain-level guarantee that the prologue was generated
 *   even if runtime hardware support varies.
 */

#include "win.h"

#include <stdio.h>

/* The leaf victim.
 *
 *   - __attribute__((naked)) : no compiler-inserted frame -> no x30 spill ->
 *     under pac-ret (the default) the function is a true leaf from the
 *     compiler's signing heuristic and gets NO paciasp. Under pac-ret+leaf
 *     the compiler emits paciasp/autiasp anyway (SignReturnAddress::All).
 *   - __attribute__((noinline)) : keep the call site real; without this
 *     -O0 is usually enough but the suite has been bitten by
 *     optimization-invariance assumptions before, so mark it explicitly.
 *   - Inline asm body sets x30 = exploit_success_t09. No trailing `ret`
 *     in the asm: under nopac/pac the compiler's epilogue is just `ret`,
 *     which consumes the corrupted x30; under pac_leaf the compiler's
 *     epilogue is `autiasp; ret`, which faults on the corrupted x30.
 *   - Do NOT add noreturn: the function must have a real epilogue (ret,
 *     or autiasp+ret under pac_leaf) for the gating observation to be
 *     visible.
 */
__attribute__((naked, noinline))
static void leaf_victim(void)
{
    asm("adrp x30, exploit_success_t09\n\t"
        "add  x30, x30, #:lo12:exploit_success_t09");
}

int main(void)
{
    leaf_victim();

    /* Reached only if leaf_victim's `ret` somehow returned control here
       without exploit_success_t09 running. On nopac/pac that should not
       happen (the corrupted X30 redirects to exploit_success_t09 which
       _exit(0)s). On pac_leaf the autiasp poisons X30 and `ret` faults,
       which also does not reach here. Any path that DOES reach here is
       a broken test setup and is flagged as ERROR by the harness via
       the UNEXPECTED: sentinel. */
    printf("UNEXPECTED:t09 leaf_victim returned\n");
    return 1;
}
