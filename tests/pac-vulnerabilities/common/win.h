/*
 * win.h -- shared exploit-success declarations for the PAC vulnerability suite
 *          (ADR-2026-05-30-005).
 *
 * These functions are the "win condition" targets that the per-test vuln.c
 * programs redirect control flow to via a corrupted saved LR or function
 * pointer. On a successful exploit each prints "PWNED:<tag>\n" to stdout and
 * calls _exit(0). On the PAC-ret build these functions are still defined and
 * signed normally; they simply are never reached on the trapping path.
 *
 * Design note: each test gets a no-argument entry point. Redirecting control
 * flow via a corrupted return address on AArch64 does not let the attacker
 * easily set x0, so a "PWNED:<tag>" target that takes an argument would
 * print garbage. Per-test entry points sidestep the issue and let the
 * harness attribute wins per test by sentinel string.
 */

#pragma once
#include <stdnoreturn.h>

/* Per-test win targets. Each prints "PWNED:tNN[suffix]\n" and _exit(0). */
noreturn void exploit_success_t01(void);
noreturn void secret_admin(void);             /* t02: prints "PWNED:t02"   */
noreturn void exploit_success_t04(void);
noreturn void exploit_success_t05a(void);
noreturn void exploit_success_t05b(void);
noreturn void exploit_success_t06(void);     /* t06: prints "PWNED:t06"   */
noreturn void exploit_success_t07(void);     /* t07: prints "PWNED:t07"   */

/* t03 entry point: only prints "PWNED:t03" iff magic == 0xC0DECAFE, proving
   that the 2-gadget ROP chain successfully loaded a controlled value into
   x19 before reaching this function. Reads the magic from x19 via asm so
   the caller does not need a calling-convention-correct argument register. */
void exploit_success_with_arg(void);
