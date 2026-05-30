/*
 * t05_negative_controls/fptr_overwrite.c -- ADR-2026-05-30-005, test 5a.
 *
 * Primitive: heap-allocated struct with an inline function-pointer field
 *   adjacent to a fixed-size buffer. An overflow into the buffer overwrites
 *   the function pointer with the address of exploit_success_t05a. A later
 *   legitimate indirect call through that pointer transfers control to the
 *   attacker target.
 *
 * Negative control: PAC-ret signs return addresses on the stack; it does
 *   NOT sign function pointers in heap data. This test is expected to "win"
 *   on BOTH builds, documenting the explicit scope boundary of pac-ret.
 *   Defeating this attack would require -mbranch-protection=pac-ret+bti
 *   (with BTI landing pads on indirect-call targets) or the newer pauthabi
 *   forward-edge protection -- both out of scope for the ADR.
 *
 * Expected outcome (this test):
 *   nopac: stdout contains "PWNED:t05a", exit 0
 *   pac:   stdout contains "PWNED:t05a", exit 0    (PAC does NOT block)
 *
 * Toolchain assumptions: GCC 13.x+, -O0, -static -no-pie,
 *   -fno-stack-protector mandatory.
 */

#include "../common/win.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"

typedef void (*action_fn)(void);

struct widget {
    char       name[16];
    action_fn  on_action;   /* adjacent to name -- the overflow target */
};

static void legitimate_action(void)
{
    /* Legitimate handler that the program would call before the bug. */
    puts("legitimate_action ran (should not appear on a successful exploit)");
}

int main(void)
{
    struct widget *w = calloc(1, sizeof(*w));
    if (!w) {
        fprintf(stderr, "UNEXPECTED:t05a calloc\n");
        return 1;
    }
    w->on_action = &legitimate_action;

    /* The bug: an unchecked copy into a fixed-size field. The trailing
       bytes spill into the adjacent on_action pointer. */
    unsigned char payload[sizeof(*w)];
    memset(payload, 0x55, sizeof(payload));
    uintptr_t target = (uintptr_t)&exploit_success_t05a;
    memcpy(payload + offsetof(struct widget, on_action),
           &target, sizeof(target));

    memcpy(w->name, payload, sizeof(payload));

    /* Indirect call through the (now corrupted) function pointer. PAC-ret
       does not protect this edge; the call lands at exploit_success_t05a
       on both nopac and pac builds. */
    w->on_action();

    /* exploit_success_t05a calls _exit(0); we should never get here on
       either build. If we do, the test setup is broken. */
    puts("UNEXPECTED:fell through");
    free(w);
    return 1;
}
