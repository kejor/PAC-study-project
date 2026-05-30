/*
 * t03_rop_2gadget/gadgets.c -- ADR-2026-05-30-005, test 3 ROP gadget.
 *
 * gadget_set_x19 is a non-leaf C function with a stack frame, so under
 * -mbranch-protection=pac-ret the compiler emits paciasp in its prologue
 * and autiasp in its epilogue. Its body loads the magic value into the
 * callee-saved register x19 and tail-branches to exploit_success_with_arg
 * (not ret -- see "Chaining note" below).
 *
 * Why a non-leaf C function and not a naked asm fragment:
 *   The ADR's canonical example is "ldr x19, [sp], #16; ret" -- a stack-pop
 *   gadget. A pure asm version would be a naked fragment that PAC has no
 *   opportunity to sign, defeating the test's purpose. To model "the
 *   realistic case where ROP chains splice into compiler-emitted epilogues"
 *   (ADR rationale), we implement the gadget as a real signed non-leaf
 *   function: x19 is loaded in inline asm and the chain target is reached
 *   via a direct branch in the same body. This keeps the gadget on the
 *   compiler's signed-function code path -- the exact path PAC-ret
 *   protects.
 *
 * Chaining note:
 *   A faithful "epilogue gadget" would chain via the natural ret using a
 *   stack slot the attacker controls. In our setup the attacker bytes
 *   overwrite the caller (victim)'s frame, but by the time gadget_set_x19
 *   runs, x29 has been loaded from one of the attacker-controlled slots,
 *   so it points into .text (a gadget address) rather than into the
 *   stack. Attempting to chain through gadget_set_x19's natural epilogue
 *   from there crashes on the rewritten saved-x29 (the str-to-x29 in the
 *   compiler's epilogue would write to .text). Using a direct `b` to
 *   exploit_success_with_arg sidesteps that one stack-pivot complication
 *   while preserving the test's actual claim: PAC must block the FIRST
 *   redirect (victim -> gadget_set_x19) for the chain to fail. That first
 *   redirect is exactly what -mbranch-protection=pac-ret defends.
 *
 * PAC behavior:
 *   nopac: paciasp/autiasp not emitted. Victim's ret jumps here; this
 *          function runs, sets x19, branches to exploit_success_with_arg.
 *   pac:   The first ret -- in victim()'s epilogue, autiasp on the
 *          attacker-overwritten unsigned LR -- traps. This function is
 *          never entered.
 *
 * Verification (per ADR acceptance criteria):
 *   `aarch64-unknown-linux-gnu-objdump -d build/pac/t03_rop_2gadget` must
 *   show at least one `paciasp` and one `autiasp` in this function.
 */

#include "../common/win.h"

#include <stdint.h>

__attribute__((noinline))
void gadget_set_x19(void)
{
    /* Force the compiler to allocate a stack frame so PAC-ret emits
       paciasp/autiasp around this function. Without a frame the function
       would be a leaf and not signed. */
    volatile unsigned long force_frame = 0;
    (void)force_frame;

    /* Load magic into x19 (callee-saved) and tail-branch to the second
       gadget. The `b` (vs `bl`) leaves the call return path alone --
       exploit_success_with_arg reads x19, prints, _exits. We never come
       back to this function, so the epilogue's autiasp does not run. */
    asm volatile (
        "mov x19, #0xCAFE\n\t"
        "movk x19, #0xC0DE, lsl #16\n\t"
        "b exploit_success_with_arg\n\t"
        ::: "x19"
    );
}
