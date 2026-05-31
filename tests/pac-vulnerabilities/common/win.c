/*
 * win.c -- exploit-success target definitions (ADR-2026-05-30-005).
 *
 * Each win function uses _exit (not exit) to skip atexit handlers and stdio
 * cleanup, so that on the nopac success path the harness sees a clean exit
 * with the "PWNED:<tag>" sentinel as the last line of stdout. fflush(stdout)
 * is called explicitly so the sentinel survives libc buffering when stdout
 * is piped to the harness.
 */

#include "win.h"

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

static noreturn void emit_and_exit(const char *tag)
{
    printf("PWNED:%s\n", tag);
    fflush(stdout);
    _exit(0);
}

noreturn void exploit_success_t01(void)  { emit_and_exit("t01"); }
noreturn void secret_admin(void)         { emit_and_exit("t02"); }
noreturn void exploit_success_t04(void)  { emit_and_exit("t04"); }
noreturn void exploit_success_t05a(void) { emit_and_exit("t05a"); }
noreturn void exploit_success_t05b(void) { emit_and_exit("t05b"); }
noreturn void exploit_success_t06(void)  { emit_and_exit("t06"); }
noreturn void exploit_success_t07(void)  { emit_and_exit("t07"); }
noreturn void exploit_success_t08(void)  { emit_and_exit("t08"); }

void exploit_success_with_arg(void)
{
    /* Read x19 (callee-saved, set by the t03 ROP gadget) into a C variable.
       Done in asm so the caller does not need to follow the AArch64 PCS for
       argument passing -- the ROP chain controls x19, not x0. */
    unsigned long magic;
    asm volatile ("mov %0, x19" : "=r"(magic));

    if (magic == 0xC0DECAFEUL) {
        printf("PWNED:t03\n");
        fflush(stdout);
        _exit(0);
    }
    printf("UNEXPECTED:t03 reached exploit_success_with_arg but x19=0x%lx\n",
           magic);
    fflush(stdout);
    _exit(1);
}
