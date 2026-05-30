/*
 * hello_corrupt.c -- PAC enforcement runtime probe (ADR-2026-05-30-004).
 *
 * Built with -mbranch-protection=pac-ret. The non-leaf function `victim()`
 * is signed by the compiler with `paciasp` in the prologue and authenticated
 * with `autiasp` in the epilogue. After signing, we deliberately overwrite
 * the saved LR slot on the stack with a bogus value so that `autiasp` will
 * see a mismatched signature and the subsequent `ret` will trap.
 *
 * Expected outcomes:
 *   - PAC enforced: process terminates via SIGILL (exit 132) or SIGSEGV
 *     (exit 139) at the `ret` in `victim`. The line "returned from victim
 *     (PAC NOT enforced)" must NEVER print on a PAC-enforcing runner.
 *   - PAC NOT enforced (instructions become hints/NOPs): the corrupted LR
 *     is followed and the program either segfaults in the weeds or, less
 *     commonly, prints the "PAC NOT enforced" line and exits 0.
 *
 * Stack LR slot offset:
 *   With aarch64-unknown-linux-gnu-gcc 13.x at -O0, the prologue of
 *   `victim()` after `paciasp` is `stp x29, x30, [sp, #-16]!` followed by
 *   `mov x29, sp`. The signed LR therefore lives at `[sp, #8]` (high half
 *   of the saved x29/x30 pair). The clobber below uses that offset. If a
 *   toolchain upgrade changes the prologue, re-pin the offset by
 *   disassembling `victim` with `aarch64-unknown-linux-gnu-objdump -d` and
 *   updating both the asm and this comment.
 */

#include <stdio.h>

__attribute__((noinline))
static void victim(void)
{
    puts("entered victim");
    /* Clobber the signed LR on the stack. The store happens after `paciasp`
       (which signs x30) and after the prologue spills x29/x30 to [sp, #0]
       and [sp, #8]. When the epilogue runs `autiasp`, it will see a bogus
       value in x30 and authentication will fail, trapping at `ret`. */
    asm volatile (
        "mov x9, #0xdead\n\t"
        "str x9, [sp, #8]\n\t"
        : : : "x9", "memory"
    );
    puts("about to return from victim");
}

int main(void)
{
    victim();
    puts("returned from victim (PAC NOT enforced)");
    return 0;
}
