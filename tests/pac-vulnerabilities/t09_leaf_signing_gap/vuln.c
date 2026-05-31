/*
 * t09_leaf_signing_gap/vuln.c -- ADR-2026-05-30-009.
 *
 * Primitive: LEAF FUNCTION X30 CORRUPTION under LLVM's default pac-ret
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
 *   The mitigation is -mbranch-protection=pac-ret+leaf, which selects
 *   SignReturnAddress::All. Under All, shouldSignReturnAddress() returns
 *   true for leaf and non-leaf functions alike, so paciasp / autiasp are
 *   emitted everywhere. This is documented in the ARM ACLE and is the
 *   configuration shipped by some downstream distributions (e.g.
 *   Android's hardened builds) precisely to close this gap.
 *
 * Why X30 is corrupted via inline asm instead of via a stack overflow.
 *   A leaf function on AArch64 at -O0 typically does not spill X30 to the
 *   stack -- "leaf" is defined precisely by "makes no calls", and X30
 *   only needs preservation across calls. Constructing a buffer overflow
 *   that reaches a saved X30 would require forcing the leaf to spill,
 *   which means giving it a call, which would make it non-leaf and
 *   defeat the purpose. A direct `mov x30, %0` inline-asm sequence inside
 *   the leaf function is the cleanest minimal model of an attacker who
 *   has achieved arbitrary write into the LR register (e.g. via a
 *   corrupted register-restore gadget, via a sibling-tail-call into
 *   attacker data, via a compiler-spill bug). The redirect target is
 *   exploit_success_t09, which prints PWNED:t09 and _exit(0)s.
 *
 * Why both nopac AND pac PWN. This is the load-bearing observation of
 *   this test. Under nopac there is no PAC at all, so the corrupted X30
 *   is consumed by `ret` unchecked. Under pac (-mbranch-protection=
 *   pac-ret, LLVM default) the leaf function's prologue/epilogue contain
 *   no paciasp/autiasp, so again `ret` consumes the corrupted X30
 *   unchecked. The pac variant succeeding demonstrates that LLVM's
 *   default pac-ret policy leaves leaf functions unsigned. Under
 *   pac_leaf (-mbranch-protection=pac-ret+leaf) the epilogue is
 *   `autiasp; ret`; autiasp poisons the corrupted X30 and the subsequent
 *   ret faults (SIGSEGV / SIGBUS / SIGILL).
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

/* The leaf victim. MUST be a real leaf (no calls inside) AND must be
   marked noinline so the compiler does not fold its epilogue into main's.
   Do NOT add noreturn: the function must have a real epilogue (ret, or
   autiasp+ret under pac_leaf) for the gating observation to be visible.

   Body: corrupt X30 directly with the address of exploit_success_t09, then
   return. Under nopac and pac the trailing `ret` jumps to the corrupted
   X30 (= exploit_success_t09). Under pac_leaf the trailing `autiasp` first
   tries to authenticate the corrupted X30 against the SP modifier; because
   the value was never signed under that modifier, autiasp poisons the
   high bits and the subsequent `ret` faults. */
__attribute__((noinline))
static void leaf_victim(void)
{
    asm volatile("mov x30, %0" :: "r"(exploit_success_t09) : "x30");
    return;
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
