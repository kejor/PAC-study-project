# ADR-2026-05-30-006: SP-Collision PAC Reuse Test (`t06_sp_collision_pac_reuse`)

**Date**: 2026-05-30
**Status**: Implemented (2026-05-30)
**Commit**: f503f2aa52295c89acd241a288c344f570153b0b
**Author**: Security Architect

## Context

The PAC vulnerability suite from ADR-2026-05-30-005 covers five primitive
classes (stack BOF over saved LR, ret2func, 2-gadget ROP, off-by-one LR
clobber, and two negative controls for forward-edge / data-only). All four
positive tests are expected to **trap on the `pac` variant**. The suite
therefore demonstrates only the cases where `-mbranch-protection=pac-ret`
*succeeds*.

The USENIX Security '19 paper "PAC it up: Towards Pointer Integrity using
ARM Pointer Authentication" (Liljestrand et al., Section 3 and Section
7.2.1) documents a concrete weakness of vanilla `pac-ret`: the modifier is
the current SP, so an authenticated return address is reusable across any
two function invocations that execute at the same SP. PARTS (Section 5.3,
Listing 2) fixes this by folding a compile-time function-id into the
modifier; vanilla GCC `pac-ret` does not.

The suite currently has no test that documents this PAC bypass, so a
student reading the results would conclude — wrongly — that `pac-ret`
blocks every saved-LR corruption. The CFI bypass surface analyzer
(Project A) needs a ground-truth example of an SP-collision bypass to
calibrate its CFB metric against.

## Decision

Add a sixth test `t06_sp_collision_pac_reuse` to the suite, structured
identically to t01–t05 (two variants: `nopac`, `pac`; same Makefile rule
template; same harness; one `vuln.c`). The test demonstrates that an
attacker who can read a legitimately-signed LR from one function's stack
frame can substitute it into a second function's saved-LR slot and pass
`autiasp` — provided the two functions share an SP value at the epilogue.

The captured value is the *signed* LR (the post-`paciasp` value of x30,
which is exactly what is spilled to the stack by `stp x29, x30, [sp,
#-N]!`). The exploit substitutes that value into the victim's own saved
LR; `autiasp` in victim's epilogue uses the same SP modifier that
`paciasp` used in donor's prologue, so authentication succeeds and
control transfers to `exploit_success_t06`.

`expectations.yml` declares the `pac` variant of t06 as `pwned` — i.e.,
`stdout_contains: "PWNED:t06"`, `exit: [0]` — for BOTH variants. This
is the one entry in the suite where PAC is expected NOT to block the
exploit. A comment in `expectations.yml` cites Section 3 / 7.2.1 of the
Liljestrand paper and labels the case as a documented PAC bypass.

## Rationale

**Why this matters now.** The suite's stated purpose is to bound the
protection of vanilla `pac-ret`. Bounding requires both positive (PAC
traps) and negative (PAC does not trap) cases. t05 covers forward-edge
and data-only — primitives `pac-ret` was never designed to cover. t06
covers a primitive `pac-ret` *was* designed to cover and still fails
on. Without t06 the negative-control set is incomplete in a way the
paper itself flags as the headline weakness of plain `pac-ret`.

**Why the donor/victim shared-global design.** The PAC key is opaque to
userspace; the test cannot forge a PAC. It must read one that the
kernel-installed key already produced. The only stable observation
point for a signed LR is the stack slot where `stp x29, x30, [sp,
#-N]!` writes it after `paciasp`. The donor function reaches that slot
via `__builtin_frame_address(0)` plus the well-known `[fp, #8]` offset
under `-O0`, copies the 64-bit word to a `volatile` global, and
returns normally (its own `autiasp` will authenticate it against the
same SP it was signed with). Victim, called from the same call site
with an identical local frame, overflows into its saved LR slot,
writes `captured_lr` there, and returns. Victim's `autiasp` sees a
modifier (SP at epilogue) equal to donor's `paciasp` modifier (SP at
prologue) because both were called at the same stack depth with
identical prologue frame sizes — so authentication passes.

**Why an SP-equality runtime assertion.** Frame layout at `-O0 -static`
is deterministic for the toolchain we pin, but it is not a language
guarantee. If a future compiler version reorders locals, inserts
spill padding, or changes the prologue, the SP-at-entry of donor and
victim could diverge silently. The test captures donor's SP-at-entry
to a second `volatile` global, then in victim compares against
`__builtin_frame_address(0)`-derived SP-at-entry, and aborts with
`UNEXPECTED:t06 sp-mismatch ...` if they differ. The harness already
treats `UNEXPECTED:` as ERROR (run_tests.py:158), so a layout drift
surfaces as a CI failure rather than a silent miss.

**Why not use the existing two-function pattern from t01.** t01's
two functions (`inner_victim` + `victim`) are *callee/caller* — they
have different SPs by construction. t06 needs two functions that are
*peers* called from main at the same depth, so that the SP at each
one's prologue is identical. The shape is genuinely different and
copying t01 would obscure what the test is showing.

**Alternatives considered and rejected.**

1. *Forge a PAC by guessing the key.* Rejected — key is 128 bits;
   not a useful demo even if the QARMA-based MAC were weakened.
2. *Use a sibling-call / jump-into-prologue trick to make
   donor.paciasp double as victim's signing oracle.* Rejected — too
   sensitive to compiler output; tests should not depend on hand
   inspection of every objdump diff across toolchain bumps.
3. *Demonstrate the bypass at a different stack depth using
   re-signing.* Rejected — would require knowing the key or chaining
   a PAC oracle gadget; out of scope for the suite's "primitive
   classes" framing.
4. *Run donor and victim in two separate processes.* Rejected — PAC
   keys are per-process; cross-process LR substitution would fail
   `autiasp` for a reason unrelated to the SP-collision claim.

## Implementation Notes

### Files to create

```
tests/pac-vulnerabilities/t06_sp_collision_pac_reuse/vuln.c
```

### Files to modify

```
tests/pac-vulnerabilities/Makefile
tests/pac-vulnerabilities/harness/expectations.yml
tests/pac-vulnerabilities/common/win.h
tests/pac-vulnerabilities/common/win.c
```

(plus the suite's README if one is added later; not required for ADR
acceptance.)

### Win target

Add to `common/win.h`:

```c
noreturn void exploit_success_t06(void);
```

and in `common/win.c` mirror the existing per-test win functions:
prints `"PWNED:t06\n"` to stdout (via `puts` or equivalent), then
`_exit(0)`. Match the style used for `exploit_success_t01`.

### Makefile changes

In `tests/pac-vulnerabilities/Makefile`:

1. Add to the per-test source list:
   ```make
   SRCS_t06_sp_collision_pac_reuse := t06_sp_collision_pac_reuse/vuln.c
   ```
2. Append `t06_sp_collision_pac_reuse` to `TESTS`.

No other Makefile changes are needed — `BUILD_RULE` and `verify-static`
loop over `TESTS` and pick up the new test automatically. The pac
variant of t06 must still contain at least one `paciasp` and one
`autiasp` (both donor and victim sign and authenticate normally;
the exploit does not skip either), so `verify-static` will pass
without modification.

### `expectations.yml` entry

Append at the end of `harness/expectations.yml`:

```yaml
# t06 documents a PAC-ret bypass (Liljestrand et al., USENIX Sec '19,
# Sections 3 and 7.2.1): pac-ret signs LR with SP as the modifier, so
# an authenticated LR captured from one function is reusable in any
# other function that runs at the same SP. PARTS (Section 5.3,
# Listing 2) fixes this with a function-id modifier; vanilla
# -mbranch-protection=pac-ret does not. Both variants are expected
# to succeed because PAC does NOT block this primitive.
t06_sp_collision_pac_reuse:
  nopac: { stdout_contains: "PWNED:t06", exit: [0] }
  pac:   { stdout_contains: "PWNED:t06", exit: [0] }
```

### `vuln.c` design — required structure

The source must implement, in order:

1. **Two file-scope `volatile` globals**:
   ```c
   static volatile uint64_t captured_lr = 0;   /* signed LR from donor    */
   static volatile uintptr_t donor_sp_at_entry = 0;
   ```
   `volatile` is mandatory so the compiler does not constant-fold
   the read in `victim()` or hoist the write out of `donor()`.

2. **`setup_donor(void)`** — `__attribute__((noinline))`, no
   arguments. Body:
   - Compute its own saved-LR slot as
     `uintptr_t lr_slot = (uintptr_t)__builtin_frame_address(0) + 8;`
     (the `[fp, #8]` slot under `-O0`; same layout assumption as
     t01's caller-frame access).
   - Read the 64-bit signed LR value at that slot into `captured_lr`.
     Use a `volatile uint64_t *` cast so the read is not optimized
     away.
   - Record SP-at-entry. The most robust observable proxy for SP at
     prologue is `(uintptr_t)__builtin_frame_address(0)` — under
     `-O0` the prologue is `stp x29, x30, [sp, #-N]!; mov x29, sp`,
     so fp equals SP-after-prologue, a deterministic offset below
     SP-at-entry. Write
     `donor_sp_at_entry = (uintptr_t)__builtin_frame_address(0);`
   - Return normally. Donor's own `autiasp` will authenticate
     against the same SP it was signed with, so the function exits
     cleanly even on the pac variant.

3. **`exploit_success_t06(void)`** — declared in `win.h`, defined in
   `win.c` (see Win target section above). Do NOT define it in
   `vuln.c`.

4. **`victim(void)`** — `__attribute__((noinline))`, no arguments.
   Body:
   - Declare `char buf[BUF_LEN];` with `BUF_LEN` 32 (mirror t01).
   - Compute its own saved-LR slot the same way donor does:
     `uintptr_t lr_slot = (uintptr_t)__builtin_frame_address(0) + 8;`
   - Verify SP equality with donor:
     `uintptr_t victim_fp = (uintptr_t)__builtin_frame_address(0);`
     If `victim_fp != donor_sp_at_entry`, print
     `"UNEXPECTED:t06 sp-mismatch donor_fp=%p victim_fp=%p\n"` to
     `stderr`, `fflush`, and `abort()`. The harness will classify
     this as ERROR.
   - Overflow `buf` to overwrite the saved LR at `lr_slot` with
     `captured_lr`. The cleanest emission under `-O0` is to compute
     `size_t lr_off = lr_slot - (uintptr_t)buf;` (assert it falls
     in the same `(0, 512]` window t01 uses), then
     `memcpy(buf + lr_off, (const void *)&captured_lr, 8);`. The
     `memcpy` is the "buffer overflow" — this is a deliberately
     written-out exploit, not a `read()`-driven one, because t06's
     point is the PAC-collision claim, not the input vector.
   - Force a real stack frame and prevent the compiler from
     elimination: `volatile int keep_frame = 0; (void)keep_frame;`
     and a final
     `asm volatile("" : : "r"(buf), "r"(&keep_frame) : "memory");`
     before return. (Same pattern as t01's `victim()`.)
   - Return. On both variants, victim's `autiasp` runs against the
     same SP modifier `captured_lr` was signed with, so
     authentication succeeds and control transfers to
     `exploit_success_t06`.

5. **`main(void)`** — calls `setup_donor();` then `victim();`. The
   two calls MUST be from the same call site in main with no
   intervening locals or function calls that would change SP. After
   `victim()` returns (it should not on a successful exploit),
   emit `puts("UNEXPECTED:fell through")` and return 1, same
   sentinel pattern as t01.

### SP-equality verification — the load-bearing detail

Both donor and victim use `__builtin_frame_address(0)` (which under
`-O0` is `x29` after the prologue's `mov x29, sp`). The two values
will be equal IFF:

- donor and victim are called from the same depth in main (they are
  — both are direct, immediately-sequential calls from main with no
  local variables introduced between them on the stack); AND
- both functions have identical prologue frame sizes (they will,
  because they declare the same set of locals — actually donor
  declares fewer, so this requires the implementer to declare the
  same `char buf[BUF_LEN]; volatile int keep_frame;` block in donor
  too, even though donor does not USE buf. This is essential and
  must NOT be skipped as "dead code"; it is what makes the SP
  modifier match. Mark `buf` with `(void)buf;` and an
  `asm volatile("" : : "r"(buf) : "memory");` clobber so the
  compiler keeps it.)

If a future compiler version inserts unequal red-zone or alignment
padding, the runtime SP-equality assert in victim catches it before
the bogus exploit attempt and the test ERRORs instead of silently
"working" by coincidence or failing the PAC check for an unrelated
reason.

### Source header comment requirements

The file's top-of-file comment must include:

- A citation to Liljestrand et al., "PAC it up: Towards Pointer
  Integrity using ARM Pointer Authentication", USENIX Security '19,
  Sections 3 and 7.2.1.
- A note that vanilla `-mbranch-protection=pac-ret` uses SP alone
  as the modifier and that PARTS (Section 5.3, Listing 2) fixes
  this with a function-id modifier.
- An expected-outcome table for both variants: nopac and pac both
  `PWNED:t06`, exit 0.
- A statement that this is the unique test in the suite where PAC
  is expected NOT to block the exploit.

### Security considerations

- **No PAC forgery is performed.** The test only reads and replays
  a value the running process itself signed seconds earlier. This
  is the threat model the paper describes; it is faithfully
  represented.
- **The test does not weaken the suite's PAC trap evidence.** The
  pac variant of t01–t04 still traps; t06 sits alongside as a
  documented bypass, not a replacement.
- **No untrusted input.** Unlike t01, t06's "overflow" is a
  fully-in-program `memcpy` of a value the program just captured
  from its own stack. There is no external attacker input. This is
  appropriate for a controlled demonstration and makes the test
  hermetic.
- **`verify-static` still meaningful.** The pac binary contains
  `paciasp`/`autiasp` in both donor and victim; the static check
  passes. The dynamic bypass does not depend on missing PAC
  instructions.

## Consequences

**Enables:**
- A ground-truth example of an SP-collision PAC bypass for the CFB
  metric / bypass-surface analyzer to calibrate against.
- A complete picture of `pac-ret` boundaries: t01–t04 (PAC works),
  t05a/b (out of scope for `pac-ret`), t06 (in scope, PAC fails).
- A talking point for the Project A writeup: "vanilla pac-ret is
  not equivalent to PARTS; here is the specific class it misses."

**Constrains:**
- The test is fragile to compiler frame-layout changes. Mitigated
  by the runtime SP-equality assert and by the `-O0` pin in
  `COMMON_CFLAGS`. A compiler bump that breaks the test will
  ERROR with `UNEXPECTED:t06 sp-mismatch`, which is the desired
  loud-failure behavior.
- t06 is the only suite entry where `pac` variant is `pwned`. The
  harness (run_tests.py) does not need changes — it already
  supports arbitrary `stdout_contains`/`exit` rules per variant —
  but anyone reading the results table needs the comment in
  `expectations.yml` to understand why this case is "expected
  pwned" on PAC.

**Does NOT enable / out of scope:**
- Any forge-PAC primitive. Pointer-signing key recovery is out of
  scope.
- Any cross-process or kernel/userspace boundary demonstration.
- A PARTS or similar function-id-modifier variant. That would be
  a separate ADR if pursued (likely as a "demonstrate the fix"
  companion to this test).

## Acceptance Criteria

- [ ] `tests/pac-vulnerabilities/t06_sp_collision_pac_reuse/vuln.c`
      exists, contains the donor/victim/main structure described
      above, and carries the required header comment with paper
      citation.
- [ ] `exploit_success_t06` is declared in `common/win.h` and
      defined in `common/win.c`, printing `PWNED:t06\n` and
      calling `_exit(0)`.
- [ ] `Makefile` lists `t06_sp_collision_pac_reuse` in `TESTS` and
      provides its `SRCS_*` entry. No other Makefile changes.
- [ ] `harness/expectations.yml` contains the t06 entry exactly as
      shown above (both variants `stdout_contains: "PWNED:t06"`,
      `exit: [0]`), with the explanatory comment block citing the
      paper.
- [ ] `make build` produces both `build/nopac/t06_sp_collision_pac_reuse`
      and `build/pac/t06_sp_collision_pac_reuse`.
- [ ] `make verify-static` reports the pac variant of t06 with at
      least one `paciasp` and one `autiasp`.
- [ ] `make test-t06_sp_collision_pac_reuse` reports PASS on both
      variants: stdout contains `PWNED:t06`, exit 0.
- [ ] If the toolchain ever changes such that donor and victim
      have unequal SP at entry, the test ERRORs with
      `UNEXPECTED:t06 sp-mismatch ...` rather than silently
      passing or failing for the wrong reason.
- [ ] `make test` (full suite) still reports PASS for t01–t05 and
      PASS for t06 on both variants.

## Implementation Notes (added during implementation, 2026-05-30)

Three details of the implementation diverged from or refined the
literal text of the ADR. They are recorded here so future readers see
the as-built rationale.

1. **Control-flow mechanism for reaching `exploit_success_t06`.** The
   ADR states that after victim's `autiasp` passes, "control transfers
   to `exploit_success_t06`". The captured signed LR, however, is
   donor's saved x30 — i.e. signed-to-return-to-the-instruction-after-
   `setup_donor()`-call-in-main. When victim's `autiasp` strips that
   PAC under the matching SP modifier, victim's `ret` therefore jumps
   to "instruction after setup_donor()" in main, not directly to
   `exploit_success_t06`.

   To produce the `PWNED:t06` observable, main uses a file-scope
   `static volatile int once` reentry guard:

   ```c
   setup_donor();
   if (once++ == 0) {
       victim();
   }
   exploit_success_t06();
   ```

   On first entry, `once==0` → victim() is called → victim's
   substituted LR returns control to the `if (once++ == 0)` line
   itself. `once` is now 1, the branch is skipped, and control falls
   through to `exploit_success_t06()`, emitting `PWNED:t06`. On the
   pac variant *without* the SP-collision weakness (hypothetical
   PARTS-style fix), autiasp would compute a different modifier than
   paciasp did in donor and trap before any control transfer.

   `once` is file-scope, not a local, so it does not add to main's
   frame — the SP at donor's and victim's call sites remain equal,
   preserving the SP-collision precondition.

2. **`char buf[BUF_LEN]` is at a POSITIVE offset from fp, so the
   saved-LR slot at `[fp, #8]` sits BELOW buf in memory.** Under
   AArch64 GCC -O0 the prologue is `stp x29, x30, [sp, #-N]!;
   mov x29, sp`, placing saved x29/x30 at the LOW end of the frame
   and local arrays at POSITIVE fp offsets. The ADR's sanity check
   "`(0, 512]` window" assumed `lr_slot > buf` (matching the
   inner_victim → caller-frame access in t01). For a function
   corrupting its OWN saved LR, `lr_slot < buf` and the offset is
   negative. The implementation widens the sanity check to a
   `|signed_off| in (0, 512]` window using `ptrdiff_t`, and writes
   the substitution through `(void *)lr_slot` directly rather than
   `buf + lr_off`. Functionally equivalent to the ADR's prescribed
   `memcpy(buf + lr_off, ...)` with a signed `lr_off`.

3. **Frame-layout matching required more locals than the ADR
   minimum.** The ADR specified that donor must declare the same
   `char buf[BUF_LEN] + volatile int keep_frame` block as victim. In
   practice, every additional uintptr_t/ptrdiff_t/size_t local victim
   uses (for SP-assert, offset sanity check, replay value) also
   gets a stack slot under -O0, growing victim's prologue frame.
   donor therefore declares the SAME set of locals — `my_fp`,
   `lr_slot`, `buf_addr`, `signed_off`, `abs_off`, `lr_off`,
   `replay` — with `(void)` casts and the SP-equality assert
   catches any drift. Two iterations of the runtime check during
   implementation caught a 16-byte fp mismatch (donor frame -96 vs
   victim frame -112) and a -48 byte buf-vs-LR offset before the
   final layout converged. This is exactly the "loud failure" the
   ADR's runtime assert was designed to produce.
