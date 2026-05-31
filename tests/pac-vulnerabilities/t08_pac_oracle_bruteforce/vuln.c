/*
 * t08_pac_oracle_bruteforce/vuln.c -- ADR-2026-05-30-008.
 *
 * Primitive: SOFTWARE PAC oracle. A SIGSEGV/SIGBUS/SIGILL handler paired
 *   with sigsetjmp/siglongjmp turns the "blraa traps on a wrong PAC"
 *   property of ARMv8.3 PAC into a crash-vs-no-crash distinguisher. With
 *   a working distinguisher the PAC field is finite-entropy and can be
 *   brute-forced one signal-delivery per wrong guess.
 *
 *   Reference: Ravichandran, Na, Lang, Wang, "PACMAN: Attacking ARM
 *   Pointer Authentication With Speculative Execution", IEEE Computer
 *   Architecture Top Picks 2022 (MIT). PACMAN's contribution is making
 *   this oracle STEALTHY via Apple-M1-specific speculative execution
 *   side channels (cache / TLB residency observed under mis-speculation,
 *   so the kernel never sees the fault). This test does NOT replicate
 *   PACMAN's speculation oracle -- M1-microarchitecture-specific
 *   conditions are not reproducible inside the project's Docker
 *   linux/arm64 environment. What this test demonstrates is the
 *   UNDERLYING logical primitive: the crash/no-crash distinguisher.
 *   A real-world attacker who can suppress / hide the crash (PACMAN via
 *   speculation; a privileged ptracer; etc.) inherits the same brute-
 *   force-feasibility property without the noise.
 *
 * Why both variants PWN. This test is not about "PAC blocks the attack";
 *   it is about "PAC's protection is bounded by its bitwidth". On the
 *   nopac variant there is no PAC to recover and the function pointer is
 *   called directly. On the pac variant the brute-force loop exhausts
 *   the 2^BRUTE_BITS candidate space and finds the correct PAC. Both
 *   variants reach exploit_success_t08 and exit 0. This is the second
 *   entry in the suite (t06 is the first) where the pac variant is
 *   expected PWNED; see ADR-2026-05-30-008 Decision section.
 *
 * Scope: BRUTE_BITS=8 (default) -> up to 256 iterations. Full PAC width
 *   on linux/arm64 with 39-bit VAs is ~24 bits (16M iterations). The
 *   8-bit reduction is purely a runtime-budget concession to the
 *   signal-handler oracle: each wrong guess costs a kernel signal
 *   delivery (~microseconds), so 24-bit search inside Docker is on the
 *   order of tens of seconds. PACMAN's speculative variant clocks the
 *   full 24-bit case at ~17 s. To run the full demonstration without
 *   source edits, rebuild with:
 *
 *     make CFLAGS_t08_pac_oracle_bruteforce__pac=" \
 *         -DUSE_PAC_BRUTE=1 -DBRUTE_BITS=24 -march=armv8.3-a" \
 *          CFLAGS_t08_pac_oracle_bruteforce__nopac="-DBRUTE_BITS=24"
 *
 *   The harness default timeout (30 s) may need extension at that scale.
 *
 *   BRUTE_BITS is a CEILING, not a floor. If the observed PAC field is
 *   narrower than BRUTE_BITS (we have measured 7 bits empirically on
 *   linux/arm64 under Apple Virtualization Framework / Docker for
 *   low-address static-binary user pointers), the brute force iterates
 *   over only the bits that are actually present. The oracle primitive
 *   demonstration is unchanged.
 *
 * PAC-mask discovery. The brute force does NOT hardcode which bits of
 *   the 64-bit pointer are the PAC bits. Instead it XORs the post-pacia
 *   value with the pre-pacia value to find bits PAC has flipped, and
 *   UNIONs the results across many modifier values. A single XOR only
 *   reveals the PAC bits whose value differs between the original and
 *   the signed pointer; the union across enough random-ish modifiers
 *   covers the entire PAC field with high probability (each bit of an
 *   n-bit PAC differs with p=1/2 per signing, so 16 signings give
 *   P[bit covered] = 1 - 2^-16). This is a TEST SCAFFOLDING SHORTCUT --
 *   a real attacker cannot read the pre-pacia value of a pointer they
 *   are trying to forge. PACMAN assumes the attacker knows the PAC bit
 *   positions from the kernel's public VA configuration, which is
 *   equivalent. The union-of-XORs trick keeps this test portable across
 *   VA configurations (39 / 42 / 48-bit VAs all yield different PAC
 *   bit ranges; the discovery mechanism is invariant).
 *
 * Signal-handler design notes.
 *   - We install handlers for SIGSEGV, SIGBUS, and SIGILL. Different
 *     kernels / hardware emit different signals on a poisoned-pointer
 *     instruction fetch: linux/arm64 typically delivers SIGSEGV from
 *     the page-fault path, but SIGBUS is observed under some
 *     configurations and SIGILL is theoretically possible if the
 *     decoded poisoned PC happens to land on an undefined encoding.
 *   - sigsetjmp(env, 1) + siglongjmp restores the signal mask. A plain
 *     setjmp/longjmp would leave SIGSEGV masked (the handler is
 *     entered with SIGSEGV in the mask) and the next bad guess would
 *     silently terminate the process. This is a well-known footgun.
 *   - SA_NODEFER guards against the recovery path itself faulting
 *     before siglongjmp; without it a nested fault would be masked
 *     into a process kill.
 *   - The handler body is async-signal-safe: it touches only a
 *     sig_atomic_t flag and the sigjmp_buf and calls siglongjmp. No
 *     allocations, no syscalls, no stdio.
 *
 * Security considerations (no PAC key recovery). The brute force
 *   recovers a single signed-value PAC under a fixed modifier. It does
 *   not extract the PAC key; the recovered value cannot be reused
 *   under a different modifier or for a different pointer. The "oracle"
 *   is internal to this process; the test does not interact with
 *   anything external. See ADR-2026-05-30-008 Security considerations.
 *
 * Hardware caveat (same as t07). The pac variant assumes real ARMv8.3
 *   PAC. On a host without it, pacia is a HINT-NOP, blraa is an
 *   unconditional indirect branch, the brute force succeeds on the
 *   first iteration, and the test PWNs for the wrong reason. The
 *   observable outcome is identical; the suite README documents this
 *   caveat.
 */

#include "win.h"

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ----- cross-handler state: sigjmp_buf + sig_atomic_t flag --------------- */

static sigjmp_buf oracle_recovery;
static volatile sig_atomic_t guess_was_wrong = 0;

static void oracle_signal_handler(int sig)
{
    (void)sig;
    guess_was_wrong = 1;
    siglongjmp(oracle_recovery, 1);
}

static void install_oracle_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = oracle_signal_handler;
    sigemptyset(&sa.sa_mask);
    /* SA_NODEFER so a fault during recovery is not silently masked into
       a process kill -- the handler is itself the recovery path. */
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS,  &sa, NULL);
    sigaction(SIGILL,  &sa, NULL);
}

/* ----- PAC inline-asm helpers, gated by USE_PAC_BRUTE -------------------- */

#if USE_PAC_BRUTE

/* pacia <Xd>, <Xn>: sign Xd under the IA-key with Xn as the modifier.
   ARMv8.3-A. Result is written back into Xd. */
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

/* blraa Xn, Xm: authenticate Xn under the IA-key with Xm as the modifier,
   then branch-with-link to the authenticated address (single instruction,
   atomic auth+branch). On correct PAC -> branch lands at the original
   target. On wrong PAC -> the resulting branch target has poisoned high
   bits and the instruction fetch faults -> SIGSEGV / SIGBUS / SIGILL.

   We pin the target into x16 and the modifier into x17 because the
   AArch64 PCS lists x16/x17 as intra-procedure-call scratch -- blraa's
   implicit clobber of those is consistent with their ABI role. x30 (LR)
   is clobbered explicitly because blraa is a branch-with-link.

   This helper RETURNS only if blraa actually branched to a callable
   target that itself returned. On the correct-PAC path in this test
   that callable target is exploit_success_t08, which _exit(0)s and
   never returns. On a wrong-PAC path the handler siglongjmps before
   we get here. */
static inline void pac_branch_authenticated(uint64_t target_with_pac,
                                            uint64_t modifier)
{
    register uint64_t tgt __asm__("x16") = target_with_pac;
    register uint64_t mod __asm__("x17") = modifier;
    __asm__ volatile (
        "blraa %[t], %[m]\n"
        :
        : [t] "r"(tgt), [m] "r"(mod)
        : "x30", "memory", "cc"
    );
}

#else  /* nopac variant: pass-through, no PAC instructions emitted. */

static inline uint64_t pac_sign(uint64_t value, uint64_t modifier)
{ (void)modifier; return value; }

static inline void pac_branch_authenticated(uint64_t target,
                                            uint64_t modifier)
{
    (void)modifier;
    void (*fn)(void) = (void (*)(void))(uintptr_t)target;
    fn();
}

#endif

/* ----- orchestration ---------------------------------------------------- */

int main(void)
{
    install_oracle_handler();

    const uint64_t target = (uint64_t)(uintptr_t)&exploit_success_t08;
    const uint64_t modifier = 0;

    /* Step 1: sign (pac variant) or pass-through (nopac variant). */
    const uint64_t signed_target = pac_sign(target, modifier);

#if USE_PAC_BRUTE
    /* Step 2 (pac variant): determine which bits of the 64-bit pointer
       are the PAC bits by UNIONing the XORs of (target signed under
       modifier i) with target, across many modifiers. A single XOR
       only reveals bits that PAC flipped for THIS particular signature;
       any PAC bit that happened to match the original-pointer bit
       stays hidden. Unioning across enough modifiers covers the full
       PAC field with high probability (each PAC bit differs with
       p=1/2 per signing, so 32 trials give P[bit missed]=2^-32).

       Test-scaffolding shortcut: a real attacker cannot sign-many-
       times-and-XOR the pointer they are trying to forge. PACMAN
       instead assumes the PAC bit positions are known from public VA
       configuration. This trick keeps the test portable across VA
       configs without hardcoding bit ranges. */

    uint64_t pac_mask = signed_target ^ target;
    for (uint64_t m = 1; m <= 32; m++) {
        pac_mask |= (pac_sign(target, m) ^ target);
    }

    /* Find the BRUTE_BITS lowest-set bits of pac_mask, low-to-high. */
    int bit_positions[BRUTE_BITS];
    int found = 0;
    for (int b = 0; b < 64 && found < BRUTE_BITS; b++) {
        if (pac_mask & (1ULL << b)) {
            bit_positions[found++] = b;
        }
    }

    /* If the PAC field is narrower than BRUTE_BITS in this environment
       (e.g. linux/arm64 under Apple Virtualization Framework empirically
       exposes ~7 PAC bits for low-address user pointers, not the textbook
       16-24 bits), the test still demonstrates the oracle primitive --
       it just iterates over the actually-present PAC bits rather than
       BRUTE_BITS. The crash/no-crash distinguisher is what matters here;
       BRUTE_BITS sets only the RUNTIME BUDGET CEILING, not a hard
       minimum entropy requirement. */
    const int brute_width = found;
    if (brute_width == 0) {
        fprintf(stderr,
                "UNEXPECTED:t08 no PAC bits detected after 33 modifier "
                "trials (pac_mask=%016llx); is real ARMv8.3 PAC enabled?\n",
                (unsigned long long)pac_mask);
        fflush(stderr);
        return 2;
    }

    /* Build low_mask from the chosen bit positions; partial is the
       signed pointer with those bits zeroed -- the oracle iterates
       reconstructions of those bits. */
    uint64_t low_mask = 0;
    for (int i = 0; i < brute_width; i++) {
        low_mask |= (1ULL << bit_positions[i]);
    }
    const uint64_t partial = signed_target & ~low_mask;

    for (uint64_t guess = 0;
         guess < (1ULL << brute_width);
         guess++) {

        /* Project the brute_width bits of `guess` into the chosen
           pac_mask positions. */
        uint64_t guess_bits = 0;
        for (int i = 0; i < brute_width; i++) {
            if (guess & (1ULL << i)) {
                guess_bits |= (1ULL << bit_positions[i]);
            }
        }
        const uint64_t candidate = partial | guess_bits;

        if (sigsetjmp(oracle_recovery, 1) == 0) {
            pac_branch_authenticated(candidate, modifier);
            /* If blraa returns (it should not on a correct guess
               because exploit_success_t08 _exit(0)s), treat as a
               late win and stop looping. */
            break;
        }
        /* Wrong guess: siglongjmp landed here. Continue loop. */
    }

    /* If we exit the loop without exploit_success_t08 running, the
       brute force failed within the chosen brute-force scope. */
    fprintf(stderr,
            "UNEXPECTED:t08 brute-force exhausted %llu candidates "
            "(brute_width=%d) without success\n",
            (unsigned long long)(1ULL << brute_width),
            brute_width);
    fflush(stderr);
    return 3;
#else
    /* nopac variant: no PAC bits to recover; just call the target. */
    pac_branch_authenticated(signed_target, modifier);
    /* Unreachable on a successful win. */
    puts("UNEXPECTED:t08 fell through (nopac)");
    return 1;
#endif
}
