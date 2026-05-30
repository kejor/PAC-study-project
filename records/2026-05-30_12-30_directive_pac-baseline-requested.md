# Directive: PAC-Enabled Baseline Requested

**Timestamp**: 2026-05-30 12:30 (UTC-5)
**Type**: Directive
**Author/Source**: User
**Tags**: directive, pac, baseline, aarch64, cfi, hardware-security

## Summary

After the no-PAC baseline passed, the user directed the project to create a second baseline with ARMv8.3 Pointer Authentication Codes (PAC) activated. This is the primary hardware CFI primitive the EE202C project will analyze for bypass surface, making this baseline the main target artifact for all subsequent analyzer work.

## Details

**User directive (paraphrased):** Create a second baseline identical in source to `baseline/aarch64-hello/` but compiled with PAC enabled, to serve as the reference PAC-hardened binary for the CFI bypass surface analyzer.

**Context at the time of the directive:**
- `baseline/aarch64-hello/` was confirmed passing (`make run` → `Hello, aarch64`, exit 0)
- The Docker `linux/arm64` runner via Apple Virtualization was established as providing real ARMv8.3 PAC hardware — a prerequisite for PAC enforcement testing
- The project needed a PAC-signed binary to analyze for bypass primitives

**Key question this directive forced:** Does the Docker `linux/arm64` container (via Apple Virtualization on Apple Silicon) actually enforce PAC authentication, or do PAC instructions silently NOP? This is not obvious and required a dedicated trap verification target in the resulting baseline.

## Rationale / Decision Basis

PAC is the hardware root of the CFI surface this project analyzes. Without a verified PAC-enforcing execution environment, bypass surface analysis would be academic. The directive to add a PAC baseline was the logical next step after confirming the runner could execute aarch64 Linux ELFs natively.

## Impact

Triggered creation of `baseline/aarch64-hello-pac/` with `-mbranch-protection=pac-ret`, `verify-static` (disassembly check), and `verify-trap` (runtime corruption test). See ADR-2026-05-30-004.

## Related Records

- `records/2026-05-30_12-00_milestone_no-pac-baseline-passing.md`
- `records/2026-05-30_12-45_adr_pac-enabled-baseline.md`
