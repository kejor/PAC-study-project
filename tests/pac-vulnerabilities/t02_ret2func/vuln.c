/*
 * t02_ret2func/vuln.c -- ADR-2026-05-30-005, test 2.
 *
 * Primitive: same stack-overflow shape as t01, but the corrupted saved LR
 *   points at secret_admin(), a function that is reachable in the binary
 *   (statically linked from common/win.c) but never called on any
 *   legitimate control-flow path inside this program. This is the classic
 *   "return-to-known-function" pattern (ret2libc minus the libc dependency).
 *
 * Two-function structure (inner_victim + victim): on AArch64 with GCC -O0
 *   the saved x29/x30 sit at the LOW end of a function's frame and locals
 *   are above them, so a forward overflow inside buf cannot reach the
 *   current function's own saved LR. We put the buffer in inner_victim()
 *   and corrupt victim()'s saved LR so the trap (or redirect) happens in
 *   victim's epilogue, before main() can run anything else. See t01 for
 *   the long-form rationale.
 *
 * Offset-to-LR scheme: runtime via __builtin_frame_address(1) (caller's
 *   fp); the saved x30 we corrupt is at [caller_fp, #8]. Aborts with
 *   "UNEXPECTED:" if the byte offset falls outside a sane window.
 *
 * Expected outcome table (this test):
 *   nopac: stdout contains "PWNED:t02", exit 0
 *   pac:   stdout does NOT contain "PWNED:t02", exit in {132, 134, 135, 139}
 *
 * Toolchain assumptions: identical to t01 (GCC 13.x+, -O0, -static -no-pie,
 *   -fno-stack-protector mandatory).
 *
 * Why this test exists alongside t01: it demonstrates PAC-ret blocks
 *   redirection regardless of whether the target is shellcode or a normal
 *   compiled function -- the primitive is the unsigned LR overwrite, not
 *   the nature of the target. The distinct "PWNED:t02" sentinel also
 *   verifies the harness attributes wins per test.
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

__attribute__((noinline))
static void inner_victim(int payload_fd)
{
    char buf[BUF_LEN];

    uintptr_t caller_fp = (uintptr_t)__builtin_frame_address(1);
    uintptr_t lr_slot   = caller_fp + 8;
    uintptr_t buf_addr  = (uintptr_t)buf;

    if (lr_slot <= buf_addr || (lr_slot - buf_addr) > 512) {
        fprintf(stderr,
                "UNEXPECTED:t02 lr-offset sanity check failed "
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
        perror("UNEXPECTED:t02 pipe");
        return 1;
    }

    size_t payload_len = BUF_LEN + 128;
    unsigned char *payload = calloc(1, payload_len);
    if (!payload) {
        fprintf(stderr, "UNEXPECTED:t02 calloc\n");
        return 1;
    }
    memset(payload, 0x42, payload_len);

    uintptr_t target = (uintptr_t)&secret_admin;
    for (size_t off = BUF_LEN; off + sizeof(uintptr_t) <= payload_len;
         off += sizeof(uintptr_t)) {
        memcpy(payload + off, &target, sizeof(target));
    }

    if (write(pipefd[1], payload, payload_len) != (ssize_t)payload_len) {
        perror("UNEXPECTED:t02 write");
        return 1;
    }
    close(pipefd[1]);
    free(payload);

    victim(pipefd[0]);

    puts("UNEXPECTED:fell through");
    return 1;
}
