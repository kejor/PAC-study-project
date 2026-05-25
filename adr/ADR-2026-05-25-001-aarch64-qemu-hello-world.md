# ADR-2026-05-25-001: aarch64 QEMU Hello World Baseline

**Date**: 2026-05-25
**Status**: Implemented
**Implemented**: 2026-05-25
**Commit**: see `git log -- adr/ADR-2026-05-25-001-aarch64-qemu-hello-world.md` (implementing commit message starts with "feat: implement ADR-2026-05-25-001")

## Context

Before building any tooling that targets aarch64 Linux binaries, we need a known-good aarch64 binary that we control end-to-end: source, build flags, and a way to execute it. That executable serves as:

1. **A verified execution baseline** — confirms the cross-compilation toolchain and QEMU setup work correctly on the developer's machine.
2. **The smallest viable input** — a hello-world binary lets us bring up any analysis or emulation pipeline without fighting binary complexity.
3. **A reproducible starting point** — every contributor (and the grader) can rebuild and rerun the same binary deterministically.

We are on macOS (darwin) hosts, so we cannot natively run aarch64 ELF binaries; QEMU is required.

## Decision

We will produce a statically-linked aarch64 Linux ELF hello-world binary and run it under **QEMU user-mode emulation** (`qemu-aarch64`).

Concrete choices:

| Concern | Choice |
|---|---|
| Source language | C (single file, `hello.c`) |
| Target triple | `aarch64-linux-gnu` |
| Toolchain | `aarch64-linux-gnu-gcc` cross-compiler (via Homebrew or a documented package) |
| Linking | **Static** (`-static`) — avoids needing a sysroot for QEMU user-mode |
| Optimization | `-O0 -g` — preserves a readable, inspectable binary |
| Binary format | ELF64 aarch64 little-endian |
| Run mode | QEMU user-mode (`qemu-aarch64 ./hello`) |
| Build entrypoint | `Makefile` at `baseline/aarch64-hello/Makefile` |
| Output location | `baseline/aarch64-hello/build/hello` |

Rejected alternatives:

- **QEMU system-mode** — too heavy for a hello-world; requires kernel, rootfs, and boot orchestration.
- **Dynamically-linked binary** — requires passing a matching aarch64 glibc sysroot to qemu-user; unnecessary complexity.
- **Assembly hello-world** — avoids the cross-compiler but is further from realistic C binaries we'll work with later.
- **Clang `--target=aarch64-linux-gnu`** — requires explicit sysroot setup; the gcc cross toolchain bundles this and is simpler.

## Rationale

- **Static + user-mode** is the shortest path from a macOS host to a running aarch64 program. It removes every moving part not needed at this stage.
- **`-O0 -g`** keeps the binary straightforward and hand-verifiable.
- **Makefile** gives a predictable extension point for adding more baseline binaries later without changing conventions.
- **`baseline/` directory** separates binary *inputs* from project tooling.

## Implementation Notes

1. **Directory structure:**
   ```
   baseline/
     aarch64-hello/
       hello.c
       Makefile
       README.md
   ```

2. **`hello.c`** — single `main` that calls `puts("Hello, aarch64")` and returns 0.

3. **Makefile** must:
   - Default to `aarch64-linux-gnu-gcc`; allow `CC` override via env var.
   - Use `CFLAGS = -O0 -g -static -Wall -Wextra`.
   - `make build` → produces `build/hello`.
   - `make run` → invokes `qemu-aarch64 build/hello`, checks exit code is 0.
   - `make clean` → removes `build/`.

4. **README.md** documents:
   - macOS install commands for the cross toolchain and `qemu-user`.
   - Expected output and exit code.
   - How to verify: `file build/hello`.
   - Exact compiler and QEMU versions used during initial bring-up.

5. **`.gitignore`**: add `baseline/*/build/` — do not commit built ELFs.

## Security Considerations

- This binary is our own trusted code, not an untrusted sample. The QEMU user-mode invocation is developer tooling.
- QEMU user-mode forwards syscalls to the host kernel. Do **not** extend this baseline to execute untrusted binaries without a separate ADR covering system-mode QEMU with an isolated rootfs.

## Consequences

**Enables:**
- A documented, reproducible aarch64 toolchain and execution environment.
- A template directory layout for additional baseline binaries in future ADRs.
- A known-correct execution reference for validating any future emulation or analysis work.

**Constraints:**
- Commits to **aarch64 Linux ELF** as the binary format for the near term.
- Commits to **QEMU user-mode** as the reference execution environment; kernel-level work would need a follow-up ADR.
- Build requires a working cross toolchain on the developer host; CI/container builds are out of scope until needed.

## Acceptance Criteria

- [ ] `baseline/aarch64-hello/hello.c`, `Makefile`, and `README.md` exist.
- [ ] `cd baseline/aarch64-hello && make build` produces `build/hello`.
- [ ] `file build/hello` reports `ELF 64-bit LSB executable, ARM aarch64, statically linked`.
- [ ] `make run` executes under `qemu-aarch64`, prints `Hello, aarch64`, and exits with status 0.
- [ ] `make clean` removes `build/`.
- [ ] README documents exact `brew install` (or equivalent) commands and toolchain versions.
- [ ] `baseline/*/build/` is in `.gitignore`; no built artifacts are committed.
