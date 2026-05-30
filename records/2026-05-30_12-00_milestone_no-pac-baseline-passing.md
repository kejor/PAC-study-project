# Milestone: No-PAC aarch64 Baseline Confirmed Passing

**Timestamp**: 2026-05-30 12:00 (UTC-5)
**Type**: Milestone
**Author/Source**: User / Agent
**Tags**: milestone, baseline, aarch64, no-pac, docker, macos, apple-silicon

## Summary

`make run` on `baseline/aarch64-hello/` printed `Hello, aarch64` and exited 0 via Docker `linux/arm64` on macOS Apple Silicon. This milestone closes the ADR-001 bring-up sequence and establishes the no-PAC control sample for the CFI bypass surface analyzer.

## Details

**Verification performed:**
- `make compile` — produced `build/hello`: static aarch64 Linux ELF, confirmed via `file`
- `make run` — Docker pulled `alpine:3.20` (one-time), ran `/build/hello` inside a `--platform linux/arm64` container, printed `Hello, aarch64`, exited 0
- No circular dependency warnings
- No toolchain fallback to `cc`/clang

**State of `baseline/aarch64-hello/` at this milestone:**
- Toolchain: `aarch64-unknown-linux-gnu-gcc` via `messense/macos-cross-toolchains`
- Compile flags: `-O0 -g -static -Wall -Wextra`
- Runner: Docker `linux/arm64` with `alpine:3.20` (Darwin); `qemu-aarch64` (Linux)
- Targets: `all`, `compile`, `run`, `clean`

**ADRs closed by this milestone:** ADR-2026-05-25-001, ADR-2026-05-30-002, ADR-2026-05-30-003 (all in `Implemented` status).

## Impact

- The no-PAC baseline is locked as the control sample. It must not be modified with PAC flags; future hardening experiments use sibling directories.
- Unblocks the PAC baseline (ADR-2026-05-30-004) and eventually the CFI bypass surface analyzer work.

## Related Records

- `records/2026-05-30_11-15_adr_macos-qemu-user-mode-workaround.md`
- `records/2026-05-30_12-30_directive_pac-baseline-requested.md`
