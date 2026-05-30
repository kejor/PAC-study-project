/*
 * t01_stack_bof_ret/vuln.c -- ADR-2026-05-30-005, test 1.
 *
 * Primitive: classic stack buffer overflow that overwrites a saved return
 *   address. The vulnerable code lives in inner_victim(): it declares
 *   `char buf[BUF_LEN]` and read()s attacker bytes into it past the end so
 *   the bytes flow forward in memory across inner_victim's frame and into
 *   the caller (victim)'s saved x29/x30 spill slots. The replacement x30
 *   is the address of exploit_success_t01(), which prints "PWNED:t01" and
 *   _exit(0).
 *
 * Why two functions (inner_victim + victim):
 *   On AArch64 with GCC -O0, the prologue is `stp x29, x30, [sp, #-N]!;
 *   mov x29, sp`, so saved x29/x30 live at the LOW end of the function's
 *   frame and locals (including buf) sit at POSITIVE offsets from fp.
 *   A forward overflow inside buf therefore grows UPWARD in memory toward
 *   the caller's frame, never reaching the current function's own saved
 *   LR. Putting the buffer in inner_victim() and corrupting victim()'s
 *   saved LR lets the trap occur in victim()'s epilogue, before main()
 *   runs any further code -- which preserves the ADR's "UNEXPECTED:fell
 *   through" sentinel as a meaningful broken-setup marker.
 *
 * Offset-to-LR scheme: discovered at runtime via __builtin_frame_address.
 *   Inside inner_victim(), the caller's (victim's) fp is
 *   __builtin_frame_address(1) and the saved x30 we want to clobber sits
 *   at [caller_fp, #8]. We compute the byte distance from &buf[0] up to
 *   that slot at runtime and abort with "UNEXPECTED:" if it falls outside
 *   a sane window (catches future toolchain layout changes early instead
 *   of producing a silent miss).
 *
 * Expected outcome table (this test):
 *   nopac: stdout contains "PWNED:t01", exit 0
 *   pac:   stdout does NOT contain "PWNED:t01", exit in {132, 134, 135, 139}
 *
 * Toolchain assumptions: aarch64-unknown-linux-gnu-gcc 13.x or newer at -O0
 *   with -static -no-pie. -fno-stack-protector is mandatory; with the
 *   canary enabled, __stack_chk_fail would catch the overflow before PAC
 *   ever runs, defeating the test's purpose. See ADR Implementation Notes.
 *
 * Hermeticity: the payload is built in-program from &exploit_success_t01
 *   (a linker-fixed address under -no-pie) and shipped to inner_victim()
 *   via a pipe; no external input file is needed. No shellcode is
 *   involved; the redirect target is a legitimate compiled function.
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

    /* Target the CALLER (victim) saved-LR slot at [caller_fp, #8]. */
    uintptr_t caller_fp = (uintptr_t)__builtin_frame_address(1);
    uintptr_t lr_slot   = caller_fp + 8;
    uintptr_t buf_addr  = (uintptr_t)buf;

    if (lr_slot <= buf_addr || (lr_slot - buf_addr) > 512) {
        fprintf(stderr,
                "UNEXPECTED:t01 lr-offset sanity check failed "
                "(buf=%p caller_fp=%p lr_slot=%p)\n",
                (void *)buf_addr, (void *)caller_fp, (void *)lr_slot);
        fflush(stderr);
        abort();
    }
    size_t lr_off = (size_t)(lr_slot - buf_addr);
    size_t want   = lr_off + sizeof(uintptr_t);

    /* The read deliberately overflows buf into caller's frame; we do NOT
       check (got == want) afterwards because the overflow itself will
       clobber stack-spilled locals (including `want`) at -O0, making a
       post-read comparison meaningless. If the read fails outright (got
       < 0) read() returns -1 in x0 which we cannot easily test without
       a stack-spilled local either; instead we trust the pipe (created
       and prefilled by main()) and let the function return -- on nopac
       the corrupted LR redirects to exploit_success_t01; on pac the
       autiasp in victim()'s epilogue traps. */
    (void)read(payload_fd, buf, want);

    asm volatile("" : : "r"(buf) : "memory");
}

__attribute__((noinline))
static void victim(int payload_fd)
{
    /* This function is the one whose saved LR will be corrupted by
       inner_victim's overflow. Its epilogue's autiasp authenticates that
       LR and traps on the pac build. On nopac the corrupted LR is
       followed to exploit_success_t01. The dummy local + the asm clobber
       force a real stack frame so the compiler still emits paciasp /
       autiasp on the pac build and a non-trivial frame layout matching
       inner_victim's caller-frame assumptions. */
    volatile int keep_frame = 0;
    (void)keep_frame;
    inner_victim(payload_fd);
    asm volatile("" : : "r"(&keep_frame) : "memory");
}

int main(void)
{
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        perror("UNEXPECTED:t01 pipe");
        return 1;
    }

    /* Spray the LR target into every 8-byte slot from BUF_LEN onward.
       Whichever slot lines up with victim's actual [fp, #8] becomes the
       redirected return address; other slots overwrite padding and
       saved-x29 (which the epilogue does not authenticate). This avoids
       a hand-counted offset for the spray, while inner_victim's runtime
       check still validates the exact lr_off it computes. */
    size_t payload_len = BUF_LEN + 128; /* covers plausible frame sizes */
    unsigned char *payload = calloc(1, payload_len);
    if (!payload) {
        fprintf(stderr, "UNEXPECTED:t01 calloc\n");
        return 1;
    }
    memset(payload, 0x41, payload_len);

    uintptr_t target = (uintptr_t)&exploit_success_t01;
    for (size_t off = BUF_LEN; off + sizeof(uintptr_t) <= payload_len;
         off += sizeof(uintptr_t)) {
        memcpy(payload + off, &target, sizeof(target));
    }

    ssize_t w = write(pipefd[1], payload, payload_len);
    if (w != (ssize_t)payload_len) {
        perror("UNEXPECTED:t01 write");
        return 1;
    }
    close(pipefd[1]);
    free(payload);

    victim(pipefd[0]);

    /* Sentinel: if victim returns on the nopac build the exploit missed,
       and the harness must flag the run as ERROR. */
    puts("UNEXPECTED:fell through");
    return 1;
}
