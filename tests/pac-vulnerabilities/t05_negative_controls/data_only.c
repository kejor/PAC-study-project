/*
 * t05_negative_controls/data_only.c -- ADR-2026-05-30-005, test 5b.
 *
 * Primitive: data-only attack. A fixed-size buffer is followed in memory
 *   by an `is_admin` integer. An overflow into the buffer flips is_admin
 *   from 0 to nonzero. A later legitimate branch on is_admin takes the
 *   "admin" arm and prints "PWNED:t05b". No control-flow integrity is
 *   ever violated: every branch in the program is at a legitimate target.
 *
 * Negative control: PAC-ret protects return-edge integrity. It has no
 *   effect on data values. This test is expected to "win" on BOTH builds,
 *   documenting that data-only attacks are outside PAC's threat model.
 *   Defending against this class requires data-flow integrity / type-safe
 *   languages / sandboxing, none of which are in scope for the ADR.
 *
 * Expected outcome (this test):
 *   nopac: stdout contains "PWNED:t05b", exit 0
 *   pac:   stdout contains "PWNED:t05b", exit 0    (PAC does NOT block)
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
#include <unistd.h>

#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"

struct session {
    char name[16];
    int  is_admin;   /* adjacent to name -- the overflow target */
};

int main(void)
{
    struct session *s = calloc(1, sizeof(*s));
    if (!s) {
        fprintf(stderr, "UNEXPECTED:t05b calloc\n");
        return 1;
    }
    s->is_admin = 0;

    /* The bug: an unchecked copy that spills past `name` into `is_admin`. */
    unsigned char payload[sizeof(*s)];
    memset(payload, 0x56, sizeof(payload));
    /* Any nonzero pattern flips is_admin; we use 0x01010101 explicitly. */
    int admin_val = 0x01010101;
    memcpy(payload + offsetof(struct session, is_admin),
           &admin_val, sizeof(admin_val));

    memcpy(s->name, payload, sizeof(payload));

    /* Legitimate, type-correct branch on is_admin. No control-flow
       integrity is violated; the program simply takes the privileged arm
       because the attacker flipped the data. */
    if (s->is_admin) {
        exploit_success_t05b();
    } else {
        puts("UNEXPECTED:is_admin still 0 -- overflow missed");
        free(s);
        return 1;
    }

    /* exploit_success_t05b calls _exit(0); unreachable. */
    free(s);
    return 0;
}
