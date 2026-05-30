# Milestone: ADR-2026-05-30-005 PAC Vulnerability Test Suite — Independent Verification Passed (12/12)

**Timestamp**: 2026-05-30 17:00 (UTC-5)
**Type**: Milestone
**Author/Source**: Agent (adr-implementation-tester)
**Tags**: milestone, pac, aarch64, cfi, vulnerability-testing, test-suite, verification, adr-2026-05-30-005, independent-review

## Summary

The adr-implementation-tester agent independently executed all 12 test cases defined in ADR-2026-05-30-005 and confirmed 12/12 passing on the first run with zero code modifications. All four return-address attack classes (t01–t04) were blocked by PAC, producing SIGSEGV (exit 139) on PAC-enabled builds as specified. Both negative controls (t05a, t05b) correctly passed on both build variants (exit 0), accurately documenting PAC's coverage limits. The suite is now independently verified and accepted as the experimental backbone for the project's CFI bypass surface analysis phase.

## Details

**Verification invocation:** `make test` from `tests/pac-vulnerabilities/` on macOS. Direct invocation via `python3 harness/run_tests.py` does not work on macOS because the Makefile exports the `DOCKER_RUN` environment variable that the harness requires to construct the Docker invocation string; this variable is absent when the script is called outside of make.

**Per-test results:**

| Test | Attack Class | no-PAC Result | PAC Result | Verified |
|------|-------------|---------------|------------|----------|
| t01  | Stack buffer overflow → return to `exploit_success_t01()` | exit 0, `PWNED:t01` printed | exit 139 (SIGSEGV) | PASS |
| t02  | ret2func → `secret_admin()` | exit 0, `PWNED:t02` printed | exit 139 (SIGSEGV) | PASS |
| t03  | 2-gadget ROP chain | exit 0, `PWNED:t03` printed | exit 139 (SIGSEGV) | PASS |
| t04  | Off-by-one LR corruption (3-byte / 16 MiB window) | exit 0, `PWNED:t04` printed | exit 139 (SIGSEGV) | PASS |
| t05a | Function-pointer overwrite (negative control) | exit 0, `PWNED:t05a` printed | exit 0, `PWNED:t05a` printed | PASS |
| t05b | Data-only attack (negative control) | exit 0, `PWNED:t05b` printed | exit 0, `PWNED:t05b` printed | PASS |

**Static analysis verification:** `make verify-static` confirmed all PAC-enabled binaries contain `paciasp`/`autiasp` instructions, proving the toolchain is emitting PAC prologues/epilogues and that the PAC variant builds are not silently falling back to unprotected code.

**Single-test targets:** All per-test `make` targets (e.g., `make test-t01`, `make test-t02`) execute correctly in isolation, confirming no hidden inter-test dependencies.

**Signal classification:** SIGSEGV (exit 139) rather than SIGBUS or SIGILL was the consistent PAC auth failure signal across all four attack tests under the QEMU-based Docker execution environment. This is consistent with how QEMU user-mode translates AArch64 PAC authentication faults and is the expected behavior for this test environment.

**No code changes required:** The implementation as committed at `5f0d72c` was correct in all respects. The tester made no modifications to any source file, Makefile, harness script, or `expectations.yml`.

## Impact

- ADR-2026-05-30-005 is now fully closed: specified, implemented, and independently verified.
- The suite is the confirmed experimental backbone for the EE202C Project A CFI bypass surface analysis deliverable.
- The macOS `make test` invocation requirement (as opposed to direct `python3 harness/run_tests.py`) is now a documented operational constraint; it should be captured in `tests/pac-vulnerabilities/README.md` to prevent confusion for future contributors.
- The consistent SIGSEGV (exit 139) signal for PAC auth failures under QEMU user-mode is now an observed, recorded fact — the project report should cite this rather than referring to SIGBUS/SIGILL as the expected signal.
- t05a/t05b passing on PAC-enabled builds is verified empirical evidence for the project report's claim that PAC provides no protection against forward-edge or data-only attacks.
- The suite is ready for extension (e.g., BTI, MTE test cases) using the same harness and `expectations.yml` pattern.

## Related Records

- `records/2026-05-30_15-30_adr_pac-vulnerability-test-suite.md` — ADR-2026-05-30-005 specification
- `records/2026-05-30_16-30_milestone_pac-vulnerability-test-suite-implemented.md` — implementation milestone (commit `5f0d72c`); documents four ADR deviations
- `records/2026-05-30_15-00_directive_adr-implementation-tester-agent-and-pipeline.md` — implementer/tester handoff pipeline this verification closes
- `records/2026-05-30_13-30_milestone_pac-baseline-passing.md` — confirmed PAC-enforcing execution environment underpinning these results
