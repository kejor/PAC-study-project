# ADR-2026-05-30-007: Explicit PAC Data-Signing Test (`t07_explicit_data_signing`)

**Date**: 2026-05-30
**Status**: Implemented (2026-05-31)
**Author**: Security Architect
**Commit**: see `git log --grep "ADR-2026-05-30-007"` (single atomic commit)

## Context

The PAC vulnerability suite (ADR-2026-05-30-005, expanded by
ADR-2026-05-30-006) currently covers seven scenarios. The two negative
controls in t05 demonstrate that `-mbranch-protection=pac-ret` does NOT
protect forward-edge function pointers (t05a) or arbitrary non-pointer
data (t05b). Both are "PAC doesn't help" cases; t05b in particular shows
that a corrupted critical data word flows through `pac-ret` undetected.

What the suite does NOT show is the complementary positive case: **PAC
hardware itself is perfectly capable of protecting arbitrary 64-bit
data**, but only when the program *explicitly* invokes the PAC sign /
authenticate instructions on that data. Compiler-managed `pac-ret`
intentionally only signs return addresses; protection of other values
requires hand-instrumented `pacia` / `autia` (or `pacda` / `autda`) inline
asm.

Choi et al., "High-Performance and Secure Data Integrity Verification
using ARM Pointer Authentication" (ICTC 2025) — the I-ACIV system — uses
exactly this primitive: chunk a kernel configuration region into 64-bit
words, sign each with `pacda` under a per-chunk modifier, store the
signed values, re-sign on verify, and compare. They report 23.6% faster
integrity checking than software SHA-256. The hardware primitive their
paper relies on is `pacia/autia` (instruction A-key) and `pacda/autda`
(data A-key) acting on user-chosen 64-bit values.

The CFI bypass surface analyzer (Project A) needs a ground-truth example
that ties the t05b "PAC doesn't protect data by default" result to the
"PAC *can* protect data if you ask it to" complement, so the CFB metric
distinguishes "PAC unused on this value" from "PAC unable to protect
this value".

## Decision

Add a seventh test `t07_explicit_data_signing` to
`tests/pac-vulnerabilities/`, structured identically to t01–t06 (two
variants `nopac` and `pac`; same Makefile rule template; same harness;
one `vuln.c`). Unlike the rest of the suite, t07's `nopac` vs `pac`
distinction is NOT the compiler flag — both variants are built with the
same `COMMON_CFLAGS`. The difference is **source-level**: the `nopac`
variant compiles `vuln.c` with a preprocessor flag disabling the inline
asm `pacia` / `autia` calls; the `pac` variant compiles the same source
with the inline asm enabled.

The program holds a "critical config value" (a `volatile uint64_t`),
signs it with `pacia` using modifier `0` (constant), simulates a 1-bit
attacker corruption of the signed value, authenticates with `autia`,
then dereferences the authenticated value as a function pointer to a
win target. Both variants run the corruption step; only the `pac`
variant has authentication that can detect it.

- `nopac` variant: no `pacia`/`autia`; corruption flips a bit in the
  stored value; the program treats the corrupted value as if it were
  the original; the dereference jumps to `exploit_success_t07` →
  prints `PWNED:t07`, exit 0.
- `pac` variant: `pacia` signs the value at startup; the same
  corruption flips a bit in the signed value; `autia` on the corrupted
  signed value produces a poisoned pointer (top bits set to the
  canonical PAC-fail pattern); the subsequent `blr` / load through
  that pointer traps with SIGSEGV/SIGBUS/SIGILL; `PWNED:t07` is NOT
  printed.

This is the **opposite** outcome of t05b. t05b: nopac pwned, pac pwned
(PAC doesn't see the data). t07: nopac pwned, pac blocked (PAC explicitly
instrumented to see the data).

The `pac` variant's outcome aligns with t01–t04 (PAC traps): exit code
one of `[132, 134, 135, 139]`, stdout excludes `PWNED:t07`.

## Rationale

**Why this matters now.** t05b documents a real limitation of `pac-ret`
but, read in isolation, invites the misreading "PAC cannot protect
data". That misreading would distort the CFB metric: it would conflate
"PAC was not asked to protect this value" with "PAC is incapable of
protecting this value". The Choi et al. I-ACIV result is a published
counter-example. The suite should carry that counter-example as a
test, not just as prose in t05b's comment.

**Why `pacia`/`autia` (instruction A-key) rather than `pacda`/`autda`
(data A-key).** The I-ACIV paper uses `pacda`/`autda` because the values
being protected are kernel configuration data, not pointers. For the
test, the value we authenticate is dereferenced as a function pointer,
so `pacia`/`autia` is the semantically correct key. The threat being
modeled — corruption of a stored signed value — is identical for either
key. Using `pacia`/`autia` also means the failure mode after a bad
`autia` is exactly the same as in t01–t04 (poisoned pointer → trap on
use), so the harness can reuse the same exit-code set without
modification. A future ADR can add a `pacda`/`autda` variant that
protects a non-pointer config word and detects corruption via explicit
compare; that is a different test.

**Why modifier `0`.** A real I-ACIV-style scheme would use a per-chunk
modifier (chunk index, base address, etc.) to prevent cross-chunk
splice attacks. For t07 the threat model is single-value corruption,
not splice; modifier `0` is the simplest reproducible choice. The
header comment must explicitly note that modifier=0 is a
*demonstration* simplification and that real deployments require a
domain-separated modifier (citing I-ACIV's per-chunk-index design).

**Why source-level toggle rather than compiler-flag toggle.** The
nopac/pac split in t01–t06 is `-mbranch-protection=pac-ret` on/off.
That works because `pac-ret` is what those tests exercise. t07
exercises explicit inline asm, which is unaffected by
`-mbranch-protection`. The cleanest knob is a preprocessor define
(`USE_PAC_DATA_SIGNING`) consumed by the `vuln.c`. The `pac` variant's
build adds `-DUSE_PAC_DATA_SIGNING=1`; the `nopac` variant does not.
Both variants are still built with `-mbranch-protection=pac-ret` in
the `pac` variant and without in the `nopac` variant for consistency
with the rest of the suite, but the load-bearing difference for t07 is
the `-D` flag.

**Why a 1-bit corruption.** A multi-bit corruption could in principle
collide with the original PAC value with probability 2^-b (where b is
the PAC bitwidth, ~24 on linux/arm64 with 39-bit VA). 1 bit is the
minimum corruption that is guaranteed to invalidate the PAC. It also
matches the canonical "fault injection" threat model that motivates
hardware integrity in the I-ACIV paper.

**Alternatives considered and rejected.**

1. *Use `pacga` (generic PAC) and `xpaclri`-style compare-and-branch.*
   Rejected — `pacga` writes its result to a GPR, requiring an
   explicit software compare with the stored signature. That's a
   valid scheme (Choi et al.'s I-ACIV is closer to this in spirit),
   but the failure mode is a branch, not a hardware trap. Reusing
   t01–t04's "trap on bad PAC" exit-code set is simpler and more
   uniform across the suite.
2. *Demonstrate at a stored function-pointer table (vtable / dispatch).*
   Rejected as scope creep — t05a already touches function pointers
   in the negative-control role; expanding it to a vtable scenario
   conflates two different things. A future ADR can layer a vtable
   test on top of t07's primitive if useful.
3. *Sign a struct of N words like I-ACIV does (chunked).* Rejected —
   the suite's tests are single-primitive demonstrations. A chunked
   integrity scheme is a *composition* of the t07 primitive, not a
   different primitive. If a chunked demo is wanted later, write a
   separate ADR.
4. *Run the corruption as an external `read()` from a file.* Rejected
   for the same reason t06's "overflow" is an in-program memcpy: t07
   is about whether the authentication catches the corruption, not
   about the input vector for the corruption.

## Implementation Notes

### Files to create

```
tests/pac-vulnerabilities/t07_explicit_data_signing/vuln.c
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
noreturn void exploit_success_t07(void);
```

and in `common/win.c` mirror the existing per-test win functions:
prints `"PWNED:t07\n"` to stdout (via `puts` or equivalent), then
`_exit(0)`. Match the style used for `exploit_success_t01`.

### Makefile changes

Two changes are needed beyond the boilerplate t01–t06 pattern, because
t07's variant split is a `-D` flag rather than `-mbranch-protection`.

1. Add to the per-test source list:
   ```make
   SRCS_t07_explicit_data_signing := t07_explicit_data_signing/vuln.c
   ```
2. Append `t07_explicit_data_signing` to `TESTS`.
3. Add **per-test extra CFLAGS** that the `BUILD_RULE` macro picks up.
   Define `CFLAGS_t07_explicit_data_signing__pac := -DUSE_PAC_DATA_SIGNING=1`
   (and leave the `nopac` variant with no extra flag — corruption
   passes through). Update `BUILD_RULE` to append
   `$(CFLAGS_$(2)__$(1))` to the compile line, e.g.:
   ```make
   $(BUILD_DIR)/$(1)/$(2): $(SRCS_$(2)) $(COMMON) common/win.h | $(BUILD_DIR)/$(1)
       $$(CC) $$($(3)) $(CFLAGS_$(2)__$(1)) -I common -o $$@ $(SRCS_$(2)) $(COMMON)
   ```
   This is additive: tests t01–t06 do not define
   `CFLAGS_<name>__<variant>` so the expansion is empty and their
   builds are unchanged. Verify by re-running the existing
   `make verify-static` and `make test` after the edit.

`verify-static` must continue to find `paciasp`/`autiasp` in the pac
binary of t07. Since the pac variant is still compiled with
`-mbranch-protection=pac-ret`, the compiler-emitted `paciasp`/`autiasp`
in `main` (and the inline-asm `pacia`/`autia` from
`USE_PAC_DATA_SIGNING`) will both be present, satisfying the static
check. No change to `verify-static` is required.

### `expectations.yml` entry

Append at the end of `harness/expectations.yml`:

```yaml
# t07 demonstrates that PAC hardware CAN protect arbitrary 64-bit data
# when the program explicitly signs/authenticates it via inline asm
# pacia/autia (instruction A-key). This is the complement to t05b
# (which shows that compiler-default pac-ret does NOT cover data).
# Reference: Choi et al., "High-Performance and Secure Data Integrity
# Verification using ARM Pointer Authentication", ICTC 2025 (I-ACIV).
t07_explicit_data_signing:
  nopac: { stdout_contains: "PWNED:t07", exit: [0] }
  pac:   { stdout_excludes: "PWNED:t07", exit: [132, 134, 135, 139] }
```

### `vuln.c` design — required structure

The source must implement the following, in order. All cross-function
state lives in **file-scope `volatile`** globals so the compiler does
not constant-fold the corruption or hoist the authentication.

1. **File header comment** must include:
   - Citation to Choi et al., I-ACIV, ICTC 2025.
   - One-paragraph explanation that this test is the complement to
     t05b: t05b shows `pac-ret` does NOT protect arbitrary data; t07
     shows that explicit `pacia`/`autia` CAN.
   - Note that modifier=0 is a demonstration simplification; real
     deployments should use a per-chunk / domain-separated modifier.
   - Expected outcome table: `nopac` = PWNED, exit 0; `pac` =
     blocked, exit in `{132, 134, 135, 139}`.

2. **The "critical config value".** Define a `volatile uint64_t
   signed_config = 0;` at file scope. On the `pac` variant it will
   hold the post-`pacia` signed value; on the `nopac` variant it will
   hold the raw target pointer.

3. **A win-trampoline pointer** — the target to dereference after
   authentication. Define
   ```c
   static volatile uint64_t target_addr = 0;
   ```
   at file scope. Initialize it in `main` to
   `(uint64_t)(uintptr_t)&exploit_success_t07;` so the literal
   address is not a compile-time constant (forces the compiler to go
   through a `volatile` read).

4. **Inline asm helpers**, gated by `USE_PAC_DATA_SIGNING`. Place
   inside `vuln.c`, not in a header — they are test-local.

   ```c
   #if USE_PAC_DATA_SIGNING
   static inline uint64_t pac_sign(uint64_t value, uint64_t modifier)
   {
       /* pacia <Xd>, <Xn>: sign value in Xd using IA-key + modifier in Xn.
          Inputs: value in any GPR, modifier in any GPR.
          Output: value with PAC inserted into the top unused VA bits. */
       uint64_t result = value;
       __asm__ volatile (
           "pacia %0, %1\n"
           : "+r"(result)
           : "r"(modifier)
       );
       return result;
   }

   static inline uint64_t pac_auth(uint64_t value, uint64_t modifier)
   {
       /* autia <Xd>, <Xn>: authenticate value in Xd using IA-key + modifier.
          On success: Xd holds the original (un-PAC'd) pointer.
          On failure: Xd holds a poisoned pointer with the canonical
          PAC-fail bits set; a subsequent load/branch through it traps. */
       uint64_t result = value;
       __asm__ volatile (
           "autia %0, %1\n"
           : "+r"(result)
           : "r"(modifier)
       );
       return result;
   }
   #else
   /* nopac variant: pass-through, no PAC instructions emitted. */
   static inline uint64_t pac_sign(uint64_t value, uint64_t modifier)
   { (void)modifier; return value; }
   static inline uint64_t pac_auth(uint64_t value, uint64_t modifier)
   { (void)modifier; return value; }
   #endif
   ```

   Both `pacia` and `autia` are ARMv8.3-A instructions. The Docker
   `linux/arm64` runtime on Apple Silicon exposes real ARMv8.3 PAC,
   so these will execute natively, not as HINT-space NOPs.

5. **`main(void)`** — orchestrates the test:

   ```c
   int main(void)
   {
       /* Step 1: populate the target pointer (volatile read to defeat
          constant folding). */
       target_addr = (uint64_t)(uintptr_t)&exploit_success_t07;

       /* Step 2: sign (pac variant) or pass-through (nopac variant). */
       const uint64_t modifier = 0;
       signed_config = pac_sign(target_addr, modifier);

       /* Step 3: simulated attacker corruption. Flip the lowest bit of
          the stored signed value. On pac: invalidates the PAC. On
          nopac: changes the target pointer to an odd address (still
          dereferenced; will land 1 byte off but the win function
          starts at an instruction boundary, so on aarch64 the BTI/
          alignment fault is NOT what we're showing -- see note
          below). */
       signed_config ^= 1ULL;

       /* Step 4: authenticate. On nopac, pass-through. On pac, autia
          a corrupted value -> poisoned pointer. */
       uint64_t authed = pac_auth(signed_config, modifier);

       /* Step 5: re-correct the low bit so the nopac path lands on a
          valid instruction address. On the pac variant this step is a
          no-op observation (poisoned bits are in the high VA range,
          not the low bit). */
       authed ^= 1ULL;

       /* Step 6: indirect call through the authenticated value. On
          pac: trap (poisoned high bits -> SIGSEGV on instruction
          fetch). On nopac: jumps to exploit_success_t07 -> PWNED:t07. */
       void (*fn)(void) = (void (*)(void))(uintptr_t)authed;
       fn();

       /* Unreachable on either variant: nopac wins above, pac traps. */
       puts("UNEXPECTED:fell through");
       return 1;
   }
   ```

   Note on Step 5: the corruption-then-recorrection of bit 0 is
   deliberate so that the *nopac* path lands on a valid instruction
   address (otherwise we'd be measuring "PC misaligned" rather than
   "PAC caught corruption"). The pac path is unaffected — `autia` on
   the corrupted value produces a high-bit-poisoned pointer, and
   flipping bit 0 of that poisoned pointer leaves the high-bit poison
   intact. The header comment must explain this so a reader does not
   misread Step 5 as undoing Step 3.

6. **No additional functions** beyond `main` and the two inline-asm
   helpers. The test deliberately holds everything in `main` to keep
   the data-flow obvious.

### Inline asm reference — `pacia` and `autia`

The two instructions used are (ARMv8.3-A):

- `PACIA Xd, Xn` — sign the value in `Xd` using the IA key with `Xn` as
  the modifier; result is written back to `Xd`.
- `AUTIA Xd, Xn` — authenticate the value in `Xd` using the IA key with
  `Xn` as the modifier. On success `Xd` contains the original pointer;
  on failure `Xd` contains a deliberately-poisoned pointer whose top
  bits cause a translation fault on use.

Both instructions are unconditional and have no flag effects. The
inline asm constraints above (`"+r"(result), "r"(modifier)`) let the
compiler choose any GPRs; GCC will pick distinct registers for the
two operands. No clobbers other than the output register are needed.

If the toolchain rejects the instruction mnemonic, add
`-march=armv8.3-a` to the per-test pac CFLAGS. Empirically GCC for
`aarch64-unknown-linux-gnu` accepts `pacia`/`autia` by default at the
versions pinned in this project, so this fallback is unlikely to be
needed; if it is, document the addition in the implementation note
below this ADR after the work lands.

### Security considerations

- **No PAC forgery.** The test only corrupts a value the program just
  signed. The PAC key is never observed, never used as an oracle, and
  never compared across processes.
- **`verify-static` semantics preserved.** The pac binary contains the
  compiler-emitted `paciasp`/`autiasp` from the `-mbranch-protection=
  pac-ret` flag (used uniformly across the suite) in addition to the
  inline-asm `pacia`/`autia`. The existing static-check loop is
  unchanged.
- **No untrusted input.** Same as t06, the corruption is an
  in-program XOR; the test is hermetic.
- **Trap-mode equivalence with t01–t04.** A failed `autia` produces a
  poisoned pointer; dereferencing it raises one of the standard PAC
  fault signals (SIGSEGV / SIGBUS / SIGILL depending on platform
  reporting). The expected-exit-code set is identical to t01–t04, so
  no harness change is needed.
- **Modifier hygiene.** The header comment must warn that modifier=0
  is a demonstration choice. Real deployments must use a
  domain-separated modifier (per-chunk index, value address, etc.)
  to defeat splice attacks within the same key.

## Consequences

**Enables:**
- A ground-truth positive example of PAC protecting non-return-address
  data, complementing t05b's negative example.
- The CFB metric can distinguish "PAC not invoked on this value" from
  "PAC unable to protect this value": t05b is the former, t07's
  `pac` variant is the negation of the former, both at fixed PAC
  hardware capability.
- A talking point for the Project A writeup: "PAC's data-integrity
  capability is real and used in the literature (I-ACIV); the suite
  measures both halves of the picture."

**Constrains:**
- t07 requires the `BUILD_RULE` macro to accept per-test extra CFLAGS.
  This is a small Makefile change that affects every test's build line
  (by adding an empty expansion). The change is additive and reversible.
- t07 introduces the first source-level (not flag-level) variant
  toggle in the suite. The header comment and ADR must call this out
  so a reader does not assume the variant split is uniformly
  `-mbranch-protection`.
- If the Docker `linux/arm64` host is run on hardware without ARMv8.3
  PAC, `pacia`/`autia` execute as HINT-space NOPs — the pac variant
  would then become `nopac`-equivalent and `t07_pac` would PWN
  unexpectedly. The harness will report this as a FAIL (not ERROR).
  Document in the suite README that t07's pac variant requires
  ARMv8.3 PAC hardware (Apple Silicon via Docker linux/arm64 is
  sufficient; the baseline `verify-trap` Makefile already asserts
  this).

**Does NOT enable / out of scope:**
- A `pacda`/`autda` data-key variant (non-pointer integrity, software
  compare). Future ADR.
- A chunked / multi-word I-ACIV-style integrity scheme. Future ADR.
- Per-chunk modifier domain separation. The test deliberately uses
  modifier=0; the comment documents the simplification.

## Acceptance Criteria

- [ ] `tests/pac-vulnerabilities/t07_explicit_data_signing/vuln.c`
      exists, contains the `pac_sign` / `pac_auth` inline-asm helpers
      gated by `USE_PAC_DATA_SIGNING`, the orchestration in `main`,
      and the required header comment with paper citation and
      modifier-0 caveat.
- [ ] `exploit_success_t07` is declared in `common/win.h` and
      defined in `common/win.c`, printing `PWNED:t07\n` and
      calling `_exit(0)`.
- [ ] `Makefile` lists `t07_explicit_data_signing` in `TESTS`,
      provides its `SRCS_*` entry, and defines
      `CFLAGS_t07_explicit_data_signing__pac := -DUSE_PAC_DATA_SIGNING=1`.
      `BUILD_RULE` is updated to expand `$(CFLAGS_$(2)__$(1))` so
      per-test extra flags compose correctly. t01–t06 builds are
      unchanged (empty expansion).
- [ ] `harness/expectations.yml` contains the t07 entry exactly as
      shown above (`nopac` PWNED exit 0, `pac` excludes PWNED with
      trap exit code), with the explanatory comment citing the
      I-ACIV paper.
- [ ] `make build` produces both `build/nopac/t07_explicit_data_signing`
      and `build/pac/t07_explicit_data_signing`.
- [ ] `aarch64-unknown-linux-gnu-objdump -d build/pac/t07_explicit_data_signing`
      contains at least one `pacia` and one `autia` mnemonic (in
      addition to `paciasp`/`autiasp` from the compiler).
- [ ] `aarch64-unknown-linux-gnu-objdump -d build/nopac/t07_explicit_data_signing`
      contains NO `pacia` / `autia` instruction (the pass-through
      branch of the `#if`).
- [ ] `make verify-static` reports the pac variant of t07 with at
      least one `paciasp` and one `autiasp` (preserved by the
      uniform `-mbranch-protection=pac-ret` flag).
- [ ] `make test-t07_explicit_data_signing` reports PASS on both
      variants: nopac stdout contains `PWNED:t07` with exit 0; pac
      stdout excludes `PWNED:t07` and exit code is in
      `{132, 134, 135, 139}`.
- [ ] `make test` (full suite) still reports PASS for t01–t06 and
      PASS for t07.

## Implementation Notes (post-implementation, 2026-05-31)

Two deviations from the original ADR text were required during
implementation; both are additive and do not alter the test semantics:

1. **`-march=armv8.3-a` was required on the t07 pac variant.** The ADR
   noted this fallback might be needed; empirically the pinned
   `aarch64-unknown-linux-gnu-gcc` toolchain rejects the bare `pacia` /
   `autia` mnemonics without it. The flag is now part of
   `CFLAGS_t07_explicit_data_signing__pac` in the Makefile.

2. **`verify-static` grep widened to accept `pacia` / `autia`.** The
   t07 pac binary contains the compiler-emitted `paciasp` / `autiasp`
   only when the compiler chooses to emit a function epilogue; for
   t07's `main` (which never returns — it ends in an indirect call
   that either wins or traps) the compiler elides the `autiasp`. The
   inline-asm `pacia` and `autia` are always present. Widening the
   patterns from `paciasp` → `paciasp|pacia` and `autiasp` →
   `autiasp|autia` is backward-compatible: t01–t06 still match on
   `paciasp` / `autiasp`, and t07 matches on `pacia` / `autia`. The
   intent of `verify-static` (confirm the PAC variant actually emits
   PAC instructions) is preserved.
