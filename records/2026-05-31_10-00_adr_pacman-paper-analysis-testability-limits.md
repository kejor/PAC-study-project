# ADR: PACMAN Paper Analysis — No New Test Cases Possible for Core Novelty

**Timestamp**: 2026-05-31 10:00 (UTC-5)
**Type**: ADR
**Author/Source**: User / Agent (paper analysis)
**Tags**: adr, pac, aarch64, pacman, speculative-execution, side-channel, cache, apple-m1, docker, qemu, testability, ee202c, isca-2022, out-of-scope

## Summary

`PAC3.pdf` was analyzed for potential new test cases. The paper is "PACMAN: Attacking ARM Pointer Authentication with Speculative Execution" (Ravichandran et al., ISCA 2022). The PDF uses substituted font encoding that makes it unreadable by `pdftotext`, but the paper was identified via metadata and its technical content is well-characterized in the literature. The conclusion is that **no new test cases can be added for PACMAN's core novelty** in this test suite. PACMAN's unique contribution — using speculative execution as a crash-free PAC oracle — requires Apple M1-specific microarchitectural gadgets, privileged cache flush instructions on AArch64 Linux, and timing precision incompatible with Docker virtualization. t08 already covers the logical primitive (crash-distinguisher oracle + iterative brute-force); PACMAN's novelty is that it achieves an equivalent oracle speculatively, without visible crashes.

## Details

**Paper:** "PACMAN: Attacking ARM Pointer Authentication with Speculative Execution"
**Authors:** Joseph Ravichandran, Weon Taek Na, Jay Lang, Mengjia Yan (MIT CSAIL)
**Venue:** ISCA 2022 (49th International Symposium on Computer Architecture)

**PDF access note:** `PAC3.pdf` uses a substituted/embedded font encoding that produces garbled output via `pdftotext`. The paper's identity and technical content were established via metadata and knowledge of the published work.

**PACMAN's core attack (what makes it novel):**

PAC entropy on AArch64 is limited (typically 7 bits for 48-bit VAs → at most 128 candidate PAC values). The known attack (t08 in this suite) iterates over all candidates using a crash-distinguisher: try each candidate, observe whether the process crashes, stop when it doesn't. PACMAN's contribution is eliminating the crash oracle entirely by exploiting speculative execution:

1. **Speculative PAC check:** Arrange for an `AUTIA` or equivalent instruction to execute speculatively on each candidate value.
2. **Microarchitectural side-channel:** Whether the speculative auth succeeds or fails is visible as a cache side-channel (load hit vs. miss pattern in TLB/D-cache following a speculative load through the potentially-authenticated pointer).
3. **Result:** All 128 candidates can be tested speculatively, non-speculatively observing only the cache state — never triggering a hardware exception. The correct PAC value is identified without a single crash.

**Why PACMAN cannot be tested in Docker/POSIX (four distinct blockers):**

| Blocker | Details |
|---------|---------|
| Apple M1-specific gadgets | PACMAN's TLB/cache observation gadgets rely on specific microarchitectural behaviors of the Apple M1's out-of-order pipeline and cache hierarchy. These gadgets were identified and validated on M1 hardware; they do not generalize to QEMU's instruction-accurate emulation (which has no microarchitectural timing model) or arbitrary AArch64 Linux servers. |
| Privileged cache flush instructions | AArch64 Linux requires `DC CIVAC` (Data Cache Clean and Invalidate to Point of Coherency) to perform reliable cache-flush-based side channels. This instruction is privileged (`EL1`+) on Linux; it generates a `SIGILL` when executed from user space. Without cache flush, the required cache state manipulation cannot be performed. |
| Docker virtualization timing noise | Cache side-channel attacks require microsecond-level timing precision. Docker (especially on macOS via virtualization) adds layers of interrupt latency, scheduling jitter, and memory access non-determinism that are incompatible with `FLUSH+RELOAD` or `PRIME+PROBE` style measurements. Even on bare-metal Linux, `rdtsc`-style timing requires careful calibration; under Docker/QEMU it is unreliable. |
| Cross-privilege scope | The fully weaponized PACMAN attack (as demonstrated in the paper) targets a kernel PAC oracle from user space — using user-space speculative execution to leak whether a kernel-authenticated pointer is valid. This cross-privilege aspect is entirely out of scope for a user-space test suite running under Docker. |

**Relationship to existing test t08:**

t08 (`pac_oracle_bruteforce`) covers the logical foundation of the PACMAN attack: given a PAC-protected pointer, iterate over all candidate PAC values and use a crash-distinguisher oracle (process exits with SIGSEGV/SIGBUS on wrong PAC, exits cleanly on correct PAC) to find the valid PAC within 128 iterations. t08 demonstrates:
- PAC entropy is bounded (7 bits in the test configuration)
- A brute-force oracle exists in the absence of FPAC hardware
- The attack is feasible in O(2^7) attempts

PACMAN's contribution is that it achieves the equivalent result **without crashes** — speculatively. This is a significant practical improvement (avoids leaving crash logs, works against targets with crash reporting, extends the attack to cross-privilege scenarios), but it does not change the fundamental result that this suite is measuring: **PAC entropy is insufficient without FPAC**.

**Decision:** No new test cases from PACMAN. t08 remains the correct representation of the entropy/oracle class in this suite.

## Rationale / Decision Basis

- **The core vulnerability class is already covered.** t08 demonstrates that PAC's limited entropy enables brute-force with a crash oracle. PACMAN's novelty is the oracle mechanism (speculative vs. crash-based), not the vulnerability class itself. For EE202C's bypass surface characterization, the class is what matters.
- **The technical prerequisites are hardware-specific and environment-incompatible.** Adding a test that cannot work in the project's Docker/QEMU environment would be a non-test: it would always "fail to demonstrate the attack" for environmental reasons, not because PAC defended against it.
- **Academic scope is appropriate.** The correct treatment for PACMAN in the EE202C report is a citation and brief explanation of why it extends t08's class but cannot be demonstrated in this environment — not a test case.
- **Alternatives considered:**
  - Add a PACMAN test that demonstrates partial primitives (cache timing): Rejected — cache timing under Docker/QEMU produces unreliable results; the test would be non-deterministic and misleading.
  - Annotate t08 to reference PACMAN in comments/README: Reasonable addition to the README, but does not require a new test case.
  - Note PACMAN as a known limitation and out-of-scope item in the project report (chosen): Correct approach.

## Impact

- No new test cases added from PACMAN analysis.
- The test suite remains at 9 test cases (t01–t09), 21 binaries — this is the complete characterization within the Docker/QEMU/AArch64v8.5 environment.
- The EE202C report should cite PACMAN as a hardware-specific extension of the entropy/oracle class (t08) and explicitly note the environmental prerequisites that place it out of scope for this test suite.
- The PDF encoding issue (substituted font in PAC3.pdf) is noted: future paper imports should be validated for text extractability before being submitted for analysis.

## Related Records

- `records/2026-05-30_20-15_adr_pac-oracle-bruteforce-test.md` — ADR-2026-05-30-008, t08 specification (entropy class, crash oracle)
- `records/2026-05-31_03-00_milestone_t08-pac-oracle-bruteforce-independent-verification.md` — t08 verification
- `records/2026-05-31_09-00_adr_no-toolchain-patches-t01-t09.md` — full classification of t01–t09 bypass surface categories
- `records/2026-05-31_07-00_milestone_t09-independent-verification.md` — full suite at 21/21 passing
