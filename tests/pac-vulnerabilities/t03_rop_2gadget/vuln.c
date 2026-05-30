/*
 * t03_rop_2gadget/vuln.c -- ADR-2026-05-30-005, test 3.
 *
 * Primitive: minimal 2-gadget ROP chain.
 *   - Stack overflow in inner_victim() corrupts the saved LR of its
 *     caller (victim) to point at gadget_set_x19 (see gadgets.c).
 *   - gadget_set_x19 sets x19 = 0xC0DECAFE and rewrites its own saved-LR
 *     slot to forward to exploit_success_with_arg, so its natural ret
 *     chains the second gadget. (On the pac build, the gadget's own
 *     autiasp catches the rewritten LR -- but we typically never get
 *     there: victim()'s epilogue traps on the first unsigned LR first.)
 *   - exploit_success_with_arg reads x19 via inline asm and only prints
 *     "PWNED:t03" if the magic matches, proving controlled register state
 *     survived the chain.
 *
 * Two-function structure: identical to t01/t02. The forward overflow in
 *   inner_victim's buf cannot reach inner_victim's own saved LR on
 *   AArch64 (locals are at positive offsets from fp), so we put the
 *   buffer in inner_victim and corrupt victim's saved LR via
 *   __builtin_frame_address(1) + 8.
 *
 * Expected outcome table (this test):
 *   nopac: stdout contains "PWNED:t03", exit 0
 *   pac:   stdout does NOT contain "PWNED:t03", exit in {132, 134, 135, 139}
 *
 * Toolchain assumptions: GCC 13.x+, -O0, -static -no-pie,
 *   -fno-stack-protector mandatory. The gadget shape in gadgets.c depends
 *   on GCC's PAC-ret prologue/epilogue emission; a toolchain swap may
 *   require updating gadgets.c (per ADR Constrains section).
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

extern void gadget_set_x19(void);

#define BUF_LEN 32

__attribute__((noinline))
static void inner_victim(int payload_fd)
{
    char buf[BUF_LEN];

    uintptr_t caller_fp = (uintptr_t)__builtin_frame_address(1);
    uintptr_t lr_slot   = caller_fp + 8;
    uintptr_t buf_addr  = (uintptr_t)buf;

    if (lr_slot <= buf_addr || (lr_slot - buf_addr) > 512) {
        fprintf(stderr,
                "UNEXPECTED:t03 lr-offset sanity check failed "
                "(buf=%p caller_fp=%p lr_slot=%p)\n",
                (void *)buf_addr, (void *)caller_fp, (void *)lr_slot);
        fflush(stderr);
        abort();
    }
    size_t lr_off = (size_t)(lr_slot - buf_addr);
    size_t want   = lr_off + sizeof(uintptr_t);

    /* See t01 for why we do not post-check (got == want): the overflow
       clobbers stack-spilled locals at -O0. */
    (void)read(payload_fd, buf, want);
    asm volatile("" : : "r"(buf) : "memory");
}

__attribute__((noinline))
static void victim(int payload_fd)
{
    volatile int keep_frame = 0;
    (void)keep_frame;
    inner_victim(payload_fd);
    asm volatile("" : : "r"(&keep_frame) : "memory");
}

int main(void)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("UNEXPECTED:t03 pipe");
        return 1;
    }

    size_t payload_len = BUF_LEN + 128;
    unsigned char *payload = calloc(1, payload_len);
    if (!payload) {
        fprintf(stderr, "UNEXPECTED:t03 calloc\n");
        return 1;
    }
    memset(payload, 0x43, payload_len);

    uintptr_t gadget1 = (uintptr_t)&gadget_set_x19;
    for (size_t off = BUF_LEN; off + sizeof(uintptr_t) <= payload_len;
         off += sizeof(uintptr_t)) {
        memcpy(payload + off, &gadget1, sizeof(gadget1));
    }

    if (write(pipefd[1], payload, payload_len) != (ssize_t)payload_len) {
        perror("UNEXPECTED:t03 write");
        return 1;
    }
    close(pipefd[1]);
    free(payload);

    victim(pipefd[0]);

    puts("UNEXPECTED:fell through");
    return 1;
}
