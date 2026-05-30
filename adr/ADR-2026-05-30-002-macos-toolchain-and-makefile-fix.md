# ADR-2026-05-30-002: macOS aarch64 Toolchain and Makefile Target Naming Fix

**Date**: 2026-05-30
**Status**: Implemented
**Implemented**: 2026-05-30
**Commit**: see `git log -- adr/ADR-2026-05-30-002-macos-toolchain-and-makefile-fix.md` (implementing commit message starts with "fix: implement ADR-2026-05-30-002")
**Author**: Security Architect

## Context

The baseline established by [ADR-2026-05-25-001](./ADR-2026-05-25-001-aarch64-qemu-hello-world.md) fails to build on the developer's macOS (Apple Silicon) host. Two independent defects surfaced from `make run`:

1. **Missing cross-compiler.** `CC ?= aarch64-linux-gnu-gcc` is overridden by macOS's environment (or simply not found on `PATH`), so make falls through to the system default `cc` (clang). Clang then fails because it cannot locate a Linux `crt0.o` — it is targeting darwin, not aarch64-linux. ADR-001 assumed a Homebrew formula named `aarch64-linux-gnu-gcc` exists; in practice no such formula is published by Homebrew core, and the developer's machine has no working cross-toolchain.
2. **Makefile circular dependency.** The phony target `build` and the build directory `build/` share a name. With `TARGET := build/hello` and the order-only dep on `$(BUILD_DIR)`, make resolves the path component `build` against the phony target and emits `Circular build/hello <- build dependency dropped`.

ADR-001 remains correct in spirit (static aarch64 ELF + QEMU user-mode); only the toolchain acquisition and the Makefile target naming need to change.

## Decision

**Toolchain.** Adopt the `messense/macos-cross-toolchains` Homebrew tap and install `aarch64-unknown-linux-gnu`. Change the Makefile default to `CC ?= aarch64-unknown-linux-gnu-gcc`. This is a precompiled gcc cross-toolchain that produces static aarch64 Linux ELFs without any additional sysroot configuration.

**Makefile.** Rename the phony target `build` to `compile`. The build *directory* remains `build/`. The default goal `all` continues to depend on the compile target.

| Concern | Before | After |
|---|---|---|
| Default `CC` | `aarch64-linux-gnu-gcc` | `aarch64-unknown-linux-gnu-gcc` |
| Phony target for compilation | `build` | `compile` |
| Build directory | `build/` | `build/` (unchanged) |
| Output binary path | `build/hello` | `build/hello` (unchanged) |
| Documented install command | (unspecified `brew install`) | `brew tap messense/macos-cross-toolchains && brew install aarch64-unknown-linux-gnu` |

Rejected alternatives:

- **Docker-based build (`dockcross/linux-arm64` or similar).** Reliable and reproducible, but pulls a container runtime dependency into a hello-world baseline. Over-engineered for the current scope; revisit if/when CI is introduced.
- **LLVM/clang with `--target=aarch64-linux-gnu` and a musl or glibc sysroot.** Works, but reintroduces the sysroot complexity that ADR-001 explicitly rejected. We would also need to bundle or download the sysroot.
- **Renaming the build directory to `out/` or `dist/`.** Equally valid and would also resolve the collision, but it churns the directory layout already documented in ADR-001 acceptance criteria. Renaming the phony target is the smaller, more local change.
- **Renaming the phony to `all` only (no `compile`).** Conflates "default goal" with "compile step." Keeping both lets future ADRs add steps (e.g., `lint`, `verify`) under `all` without further renames.

## Rationale

- The `messense/macos-cross-toolchains` tap is the de facto solution for aarch64-linux cross-compilation on macOS; it ships precompiled binaries for both Intel and Apple Silicon hosts and produces ELFs that `qemu-aarch64` runs without modification.
- Renaming the phony target (not the directory) is the minimal-diff fix and keeps every path and artifact documented in ADR-001 intact.
- Both fixes are reversible: the `CC` variable can be re-pointed at any other compiler via env override, and the phony rename touches only Makefile internals.

## Implementation Notes

**Files to modify:**

1. `/Users/kejor/ee202c/project/baseline/aarch64-hello/Makefile`
   - Change line 4: `CC ?= aarch64-unknown-linux-gnu-gcc`
   - Change line 12: `.PHONY: all compile run clean`
   - Change line 14: `all: compile`
   - Change line 16: `compile: $(TARGET)`
   - Change line 24: `run: compile`

2. `/Users/kejor/ee202c/project/baseline/aarch64-hello/README.md` (if it exists)
   - Replace the install command with:
     ```
     brew tap messense/macos-cross-toolchains
     brew install aarch64-unknown-linux-gnu
     brew install qemu
     ```
   - Update any references from `make build` to `make compile` (or just document `make all` / `make run`).
   - Pin the toolchain version used during bring-up by recording `aarch64-unknown-linux-gnu-gcc --version` output.

**Gotchas:**

- The tap publishes the toolchain under the triple `aarch64-unknown-linux-gnu`, not `aarch64-linux-gnu`. The `gcc` binary is `aarch64-unknown-linux-gnu-gcc`. Using the wrong triple will silently fall back to `cc` again.
- After `brew install`, confirm the binary is on `PATH`: `which aarch64-unknown-linux-gnu-gcc`. Homebrew on Apple Silicon installs to `/opt/homebrew/bin`; ensure the user's shell has that on `PATH`.
- Do not commit a hardcoded absolute path to the compiler; keep `CC ?=` so CI or contributors on Linux can override with `CC=aarch64-linux-gnu-gcc make`.
- The `compile` target name is conventional but not universal; the README must call it out so contributors don't reflexively type `make build`.

**Security considerations:**

- The `messense/macos-cross-toolchains` tap is a third-party Homebrew tap, not a Homebrew-core formula. It is a supply-chain dependency: a malicious tap could ship a backdoored compiler that injects code into every aarch64 binary we build. Mitigations:
  - Pin the tap revision (`brew tap messense/macos-cross-toolchains <ref>`) once a known-good version is identified during bring-up, and record the SHA in the README.
  - Treat the toolchain output as trusted-input only for our own baseline binaries. We never run the cross-compiler on untrusted source.
- No change to the runtime threat model from ADR-001: QEMU user-mode still forwards syscalls to the host kernel, and the policy of not executing untrusted binaries under this baseline is unchanged.

## Consequences

**Enables:**
- `make run` succeeds end-to-end on macOS without manual `CC=` exports.
- Eliminates the `make` circular-dependency warning, which currently masks any real warnings a developer might need to see.
- Establishes the `compile` / `run` / `clean` target vocabulary as the convention for future `baseline/*/Makefile`s.

**Constraints:**
- Adds a third-party Homebrew tap as a documented project dependency.
- Existing muscle memory or scripts that invoke `make build` will break; this is intentional and limited to the developer's local workflow.
- The toolchain triple `aarch64-unknown-linux-gnu` differs from the triple `aarch64-linux-gnu` that ADR-001 documented in its rationale. Future ADRs referencing the triple should use the new one for the macOS toolchain while noting that Linux distros typically ship `aarch64-linux-gnu-gcc`.

## Acceptance Criteria

- [ ] `brew tap messense/macos-cross-toolchains && brew install aarch64-unknown-linux-gnu` succeeds on the developer's macOS host.
- [ ] `which aarch64-unknown-linux-gnu-gcc` returns a valid path.
- [ ] `cd /Users/kejor/ee202c/project/baseline/aarch64-hello && make` (default goal) produces `build/hello` with no `Circular ... dependency dropped` warning.
- [ ] `file build/hello` reports `ELF 64-bit LSB executable, ARM aarch64, ..., statically linked`.
- [ ] `make run` prints `Hello, aarch64` and exits 0.
- [ ] `make clean` removes `build/`.
- [ ] `make build` no longer exists as a target (replaced by `make compile`); README reflects this.
- [ ] README documents the tap install command and records the `aarch64-unknown-linux-gnu-gcc --version` output captured during bring-up.
