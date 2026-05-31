/*
 * t07_explicit_data_signing/vuln.c -- ADR-2026-05-30-007.
 *
 * Primitive: explicit ARMv8.3 PAC data signing of an arbitrary 64-bit
 *   value via inline-asm `pacia` / `autia` (IA-key). The program holds a
 *   "critical config value" (a pointer to a win-target function), signs
 *   it with `pacia` under a constant modifier, simulates a 1-bit attacker
 *   corruption of the signed value, authenticates with `autia`, and then
 *   uses the authenticated value as an indirect-call target. On the pac
 *   variant the corruption invalidates the PAC, `autia` produces a
 *   poisoned pointer, and the subsequent branch traps. On the nopac
 *   variant the inline-asm path is `#if`'d out (pass-through) and the
 *   program reaches the win target.
 *
 *   Reference: Choi, et al., "High-Performance and Secure Data Integrity
 *   Verification using ARM Pointer Authentication", ICTC 2025 (I-ACIV).
 *   I-ACIV chunks a kernel configuration region into 64-bit words and
 *   uses `pacda`/`autda` (data A-key) with per-chunk modifiers to detect
 *   tampering at 23.6% faster than software SHA-256. This test
 *   demonstrates the same primitive on the IA-key (because the value
 *   protected here is dereferenced as a function pointer, not used as
 *   data), restricted to a single value with modifier=0. See the
 *   modifier note below.
 *
 * Complement to t05b: t05b shows that the compiler's default
 *   `-mbranch-protection=pac-ret` does NOT cover arbitrary non-return-
 *   address data -- a corrupted critical data word flows through
 *   `pac-ret` undetected. t07 shows the other half of the picture: PAC
 *   *hardware* is perfectly capable of protecting arbitrary 64-bit data
 *   when the program explicitly invokes `pacia`/`autia`. Together,
 *   t05b + t07 distinguish "PAC not invoked on this value" from "PAC
 *   unable to protect this value" -- a distinction the CFB metric
 *   relies on (ADR-2026-05-30-005 / Project A).
 *
 * Variant split (NOTE: differs from the rest of the suite).
 *   t01-t06 distinguish nopac vs pac via `-mbranch-protection=pac-ret`
 *   on/off. t07 keeps that flag on both variants for build uniformity,
 *   but the load-bearing difference is the preprocessor macro
 *   `USE_PAC_DATA_SIGNING`. The pac variant is compiled with
 *   `-DUSE_PAC_DATA_SIGNING=1` and uses real `pacia`/`autia`; the nopac
 *   variant is compiled without it and uses pass-through helpers.
 *   `pac-ret` is unaffected by `USE_PAC_DATA_SIGNING` -- it only
 *   protects return addresses, which is not what this test exercises.
 *
 * Modifier=0 caveat. This test deliberately uses `modifier = 0` as the
 *   simplest reproducible choice. A real I-ACIV-style deployment MUST
 *   use a domain-separated modifier (per-chunk index, value address,
 *   etc.) to defeat splice attacks within the same key -- otherwise an
 *   attacker who controls one signed value can swap it for another
 *   signed under the same modifier and the authentication still
 *   succeeds. I-ACIV uses the chunk index as the modifier; see Choi et
 *   al., Section 4.
 *
 * Step 5 (bit-0 re-XOR) note. After signing-then-corrupting bit 0 of
 *   the stored value, we XOR bit 0 back in the post-`autia` result.
 *   This is deliberate so that the *nopac* path lands on a valid
 *   instruction address (an odd address would trap on PC-misaligned
 *   instruction fetch, which would falsely look like a PAC trap to a
 *   skim reader). The pac path is unaffected: `autia` on a corrupted
 *   value produces a poisoned pointer whose poison lives in the high
 *   VA bits, and flipping bit 0 of that poisoned pointer leaves the
 *   high-bit poison intact -- the dereference still traps.
 *
 * Expected outcomes:
 *   nopac: stdout contains "PWNED:t07", exit 0.
 *   pac:   stdout EXCLUDES  "PWNED:t07", exit in {132, 134, 135, 139}
 *          (128 + SIGILL / SIGABRT / SIGBUS / SIGSEGV).
 */

#include "win.h"

#include <stdint.h>
#include <stdio.h>

/* The "critical config value": holds the post-`pacia` signed value on
   the pac variant, or the raw target pointer on the nopac variant.
   `volatile` keeps the compiler from constant-folding the corruption
   or hoisting the authentication across the XOR step. */
static volatile uint64_t signed_config = 0;

/* The target the authenticated value is dereferenced into. Held in a
   `volatile` so the compile-time address of `exploit_success_t07` is
   read through memory rather than emitted as an immediate. */
static volatile uint64_t target_addr = 0;

/* ----- PAC inline-asm helpers, gated by USE_PAC_DATA_SIGNING ------------- */

#if USE_PAC_DATA_SIGNING

/* pacia <Xd>, <Xn>: sign the value in Xd using the IA-key with Xn as the
   modifier; result written back to Xd. ARMv8.3-A. */
static inline uint64_t pac_sign(uint64_t value, uint64_t modifier)
{
    uint64_t result = value;
    __asm__ volatile (
        "pacia %0, %1\n"
        : "+r"(result)
        : "r"(modifier)
    );
    return result;
}

/* autia <Xd>, <Xn>: authenticate the value in Xd using the IA-key with
   Xn as the modifier. On success Xd holds the original pointer; on
   failure Xd holds a deliberately-poisoned pointer whose top bits
   raise a translation fault on subsequent use. ARMv8.3-A. */
static inline uint64_t pac_auth(uint64_t value, uint64_t modifier)
{
    uint64_t result = value;
    __asm__ volatile (
        "autia %0, %1\n"
        : "+r"(result)
        : "r"(modifier)
    );
    return result;
}

#else  /* nopac variant: pass-through, no PAC instructions emitted. */

static inline uint64_t pac_sign(uint64_t value, uint64_t modifier)
{ (void)modifier; return value; }

static inline uint64_t pac_auth(uint64_t value, uint64_t modifier)
{ (void)modifier; return value; }

#endif

/* ------------------------------ orchestration --------------------------- */

int main(void)
{
    /* Step 1: populate the target pointer via a volatile read so its
       value is not a compile-time constant. */
    target_addr = (uint64_t)(uintptr_t)&exploit_success_t07;

    /* Step 2: sign (pac variant) or pass-through (nopac variant). */
    const uint64_t modifier = 0;
    signed_config = pac_sign(target_addr, modifier);

    /* Step 3: simulated single-bit attacker corruption of the stored
       value. On the pac variant this invalidates the PAC; on the nopac
       variant it makes the target address odd (we re-correct it in
       step 5 so the nopac path lands on a valid instruction). */
    signed_config ^= 1ULL;

    /* Step 4: authenticate. On nopac this is a pass-through; on pac
       `autia` of a corrupted signed value yields a high-bit-poisoned
       pointer that will trap on use. */
    uint64_t authed = pac_auth(signed_config, modifier);

    /* Step 5: re-correct bit 0 so the nopac path branches to an aligned
       instruction. On the pac variant the poison lives in the high VA
       bits and is undisturbed by this XOR -- step 6 still traps. */
    authed ^= 1ULL;

    /* Step 6: indirect call through the authenticated value.
       nopac: jumps to exploit_success_t07 -> prints "PWNED:t07" -> _exit(0).
       pac:   traps on instruction fetch from poisoned address. */
    void (*fn)(void) = (void (*)(void))(uintptr_t)authed;
    fn();

    /* Unreachable on either variant: nopac wins above, pac traps. */
    puts("UNEXPECTED:t07 fell through");
    return 1;
}
