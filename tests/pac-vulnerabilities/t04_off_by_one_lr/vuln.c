/*
 * t04_off_by_one_lr/vuln.c -- ADR-2026-05-30-005, test 4.
 *
 * Primitive: a tight loop with the classic off-by-N bug shape
 *     for (i = 0; i < lr_off + EXTRA; i++) buf[i] = input[i];
 *   This walks EXTRA bytes past the saved-LR slot in the CALLER's frame,
 *   overwriting the LOW EXTRA bytes of the caller's saved LR while
 *   leaving the upper (8 - EXTRA) bytes intact.
 *
 * Width choice (per ADR fallback clause):
 *   The ADR's canonical primitive is a 1-byte off-by-one (256-byte
 *   window). Reliably redirecting via a 1-byte flip requires linker-script
 *   or alignment gymnastics to place an exploit target within 256 bytes
 *   of the legitimate return site. The ADR explicitly permits widening to
 *   a small off-by-N if 1-byte placement is brittle ("if reliable 1-byte
 *   redirection cannot be arranged with the static toolchain, the
 *   implementer may widen to a 2-byte off-by-N ... document the chosen
 *   width in the source file header"). This test uses a 3-byte width
 *   (16 MiB window). With -static -no-pie the entire .text section sits
 *   inside one 16 MiB window for this small binary, so the corrupted LR
 *   reliably lands inside the program's code. 5 of 8 LR bytes remain
 *   intact, so this still models a partial corruption -- the point of
 *   the test is that PAC fails authentication even on a small flip.
 *
 * Three-function structure (main + victim + inner_victim):
 *   On AArch64 with -O0 the buffer is at fp+positive_offset, so a forward
 *   overflow cannot reach the current function's saved LR -- the overflow
 *   grows toward the caller's frame. We put the loop in inner_victim()
 *   and corrupt the caller (victim)'s saved LR. The attacker `input`
 *   array is constructed in victim() and passed by pointer, NOT built
 *   inside inner_victim(), so the overflow does not clobber the pointer
 *   itself (which would crash a subsequent free) and inner_victim does no
 *   work after the loop that could touch stack-spilled locals.
 *
 * Offset-to-LR scheme: runtime via __builtin_frame_address(1) (caller's
 *   fp); the saved x30 is at [caller_fp, #8]. The loop bound is lr_off
 *   plus OVERWRITE_WIDTH so the last byte written lands at
 *   lr_slot + (OVERWRITE_WIDTH - 1).
 *
 * Expected outcome table (this test):
 *   nopac: stdout contains "PWNED:t04", exit 0
 *   pac:   stdout does NOT contain "PWNED:t04", exit in {132, 134, 135, 139}
 *
 * Toolchain assumptions: GCC 13.x+, -O0, -static -no-pie,
 *   -fno-stack-protector mandatory.
 */

#include "../common/win.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wframe-address"

#define BUF_LEN 32
#define OVERWRITE_WIDTH 3  /* low N bytes of saved LR clobbered */

/* The input buffer is sized to be larger than any plausible lr_off so
   inner_victim never reads past it. We don't know lr_off at compile time
   but lr_off + OVERWRITE_WIDTH is well under 256 in practice. */
#define INPUT_LEN 256

/* Perform the actual buggy copy in a helper function whose frame sits
   BELOW the overflow target (the caller's saved-LR slot is above buf,
   which is above this helper's frame). The loop counter `i` and the
   length parameter `n` live in do_overflow_copy's frame and are
   therefore not clobbered when buf overflows forward. Without this
   separation, the loop counter ends up on the stack just past buf and
   gets overwritten mid-copy, terminating the loop early. */
__attribute__((noinline))
static void do_overflow_copy(char *buf, const unsigned char *input, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = (char)input[i];
    }
}

__attribute__((noinline))
static void inner_victim(const unsigned char *input)
{
    char buf[BUF_LEN];

    uintptr_t caller_fp = (uintptr_t)__builtin_frame_address(1);
    uintptr_t lr_slot   = caller_fp + 8;
    uintptr_t buf_addr  = (uintptr_t)buf;

    if (lr_slot <= buf_addr || (lr_slot - buf_addr) > 512) {
        fprintf(stderr,
                "UNEXPECTED:t04 lr-offset sanity check failed "
                "(buf=%p caller_fp=%p lr_slot=%p)\n",
                (void *)buf_addr, (void *)caller_fp, (void *)lr_slot);
        fflush(stderr);
        abort();
    }
    size_t lr_off   = (size_t)(lr_slot - buf_addr);
    size_t copy_len = lr_off + OVERWRITE_WIDTH;

    if (copy_len > INPUT_LEN) {
        fprintf(stderr, "UNEXPECTED:t04 copy_len %zu > INPUT_LEN %d\n",
                copy_len, INPUT_LEN);
        fflush(stderr);
        abort();
    }

    /* The bug: an unchecked copy length. do_overflow_copy walks
       OVERWRITE_WIDTH bytes past buf[BUF_LEN-1] into the caller's
       saved-LR slot. We do no work after the call -- victim()'s
       epilogue follows the corrupted LR. */
    do_overflow_copy(buf, input, copy_len);
}

__attribute__((noinline))
static void victim(uintptr_t target)
{
    /* Build the attacker input in this frame (NOT in inner_victim's
       frame, which gets clobbered). Pass it down by pointer. */
    /* Padding bytes are 0x00 -- crucial, NOT some printable value. The
       overflow walks past lr_slot+OVERWRITE_WIDTH and any padding bytes
       just after the target's low 3 bytes also land on (lr_slot+3,
       lr_slot+4, ...). The original LR's upper 5 bytes are 0 (the
       legitimate return address is below 16 MiB in this small static
       binary), so zero padding leaves those bytes intact and the
       corrupted LR equals the exploit-target's low 3 bytes OR'd with
       the original upper 5 bytes. With non-zero padding we would
       clobber the upper bytes and the corrupted LR would be a wild
       non-text address. */
    unsigned char input[INPUT_LEN];
    memset(input, 0x00, sizeof(input));

    /* We don't know inner_victim's lr_off in advance, so we spray the
       low OVERWRITE_WIDTH bytes of `target` at every 8-byte-aligned
       slot in the tail of `input`. inner_victim copies (lr_off +
       OVERWRITE_WIDTH) contiguous bytes from input into its own buf;
       whichever bytes land on the saved-LR slot depend on lr_off, and
       with the spray, input[lr_off..lr_off+OVERWRITE_WIDTH-1] equals
       target's low bytes for any lr_off that is a multiple of 8 (which
       it always is under this toolchain because the prologue aligns
       frames to 16 bytes and saved x29 is the lowest 16-byte-aligned
       slot in the caller's frame). */
    for (size_t off = 8; off + sizeof(uintptr_t) <= sizeof(input); off += 8) {
        for (int i = 0; i < OVERWRITE_WIDTH; i++) {
            input[off + i] = (unsigned char)(target >> (8 * i));
        }
    }

    inner_victim(input);

    /* Sentinel for the pac build's "trap missed" failure mode. On the
       nopac success path the corrupted LR has already redirected us
       out of victim()'s epilogue before this puts() runs. */
    asm volatile("" : : "r"(input) : "memory");
}

int main(void)
{
    uintptr_t target  = (uintptr_t)&exploit_success_t04;
    uintptr_t main_a  = (uintptr_t)&main;
    uintptr_t mask_hi = ~(uintptr_t)0 << (8 * OVERWRITE_WIDTH);

    /* Sanity: the redirect only works if the legitimate return address
       (somewhere inside main()) and the exploit target share the upper
       bytes outside the overwrite window. Compare against &main as a
       proxy; in this small -static -no-pie binary both live in the same
       .text section, well within one 16 MiB window. */
    if ((main_a & mask_hi) != (target & mask_hi)) {
        fprintf(stderr,
                "UNEXPECTED:t04 main and exploit_success_t04 not in "
                "same %d-byte window (main=%p target=%p)\n",
                OVERWRITE_WIDTH, (void *)main_a, (void *)target);
        fflush(stderr);
        return 1;
    }

    victim(target);

    puts("UNEXPECTED:fell through");
    return 1;
}
