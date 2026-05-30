# ADR-2026-05-30-003: macOS QEMU User-Mode Workaround via Docker linux/arm64

**Date**: 2026-05-30
**Status**: Implemented
**Implemented**: 2026-05-30
**Commit**: see `git log -- adr/ADR-2026-05-30-003-macos-qemu-user-mode-workaround.md` (implementing commit message starts with "feat: implement ADR-2026-05-30-003")
**Author**: Security Architect

## Context

After [ADR-2026-05-30-002](./ADR-2026-05-30-002-macos-toolchain-and-makefile-fix.md) made the cross-compiler work on macOS Apple Silicon, `make run` still fails:

```
/bin/sh: qemu-aarch64: command not found
qemu-aarch64 exited with status 127
```

Homebrew's `qemu` formula (currently v11.0.1) ships only `qemu-system-*` binaries on macOS. It does **not** build the user-mode emulators (`qemu-aarch64`, `qemu-arm`, etc.) because QEMU user-mode forwards Linux syscalls to a Linux host kernel via mechanisms such as `binfmt_misc` and the Linux syscall ABI. Those primitives do not exist on Darwin, so `qemu-user` is not portable to macOS and Homebrew does not package it. On the developer's Apple Silicon host, `/opt/homebrew/bin/` contains `qemu-system-aarch64`, `qemu-img`, `qemu-io`, `qemu-nbd`, `qemu-storage-daemon`, `qemu-edid` — and nothing else.

The cross-compiled artifact at `baseline/aarch64-hello/build/hello` is a valid static aarch64 Linux ELF (verified via `file`); the toolchain is fine. The gap is purely the execution environment on macOS.

ADR-2026-05-25-001 selected `qemu-aarch64` user-mode as the reference execution environment. That choice remains correct for Linux hosts and CI. On macOS we need an equivalent that:

- Runs a **statically-linked aarch64 Linux ELF** with no kernel/rootfs orchestration.
- Has **near-native speed** on Apple Silicon (no software emulation overhead).
- Is **scriptable from a Makefile** as a single command — no interactive VM setup.
- Adds **minimal new dependencies** beyond what a security/IoT class student is likely to already have.

## Decision

Use **Docker Desktop with `--platform linux/arm64`** as the macOS execution path for aarch64 Linux binaries. The `QEMU` Makefile variable becomes a generic "runner" abstraction with a platform-aware default, and the `run` target invokes the configured runner against `$(TARGET)`.

Concrete choices:

| Concern | Choice |
|---|---|
| macOS execution backend | `docker run --rm --platform linux/arm64 -v $(PWD)/build:/build alpine:3.20 /build/hello` |
| Linux execution backend | `qemu-aarch64 $(TARGET)` (unchanged from ADR-001) |
| Default selection | Auto-detect host OS in Makefile; allow override via `RUNNER=` |
| Container image | `alpine:3.20` (small, stable; only used as a thin Linux loader for the static binary) |
| Variable name | Rename `QEMU` → `RUNNER` in the Makefile; the variable is no longer always QEMU |
| Backward compatibility | Setting `RUNNER=qemu-aarch64 make run` still works on Linux hosts |

Rejected alternatives:

- **`qemu-system-aarch64` + minimal Linux VM (buildroot/Alpine).** Requires shipping or building a kernel image, a rootfs, and a virtfs or scp path for the binary. For a hello-world this is gross over-engineering, and it adds a multi-hundred-MB artifact to the repo or a fragile download step.
- **Lima / UTM / Parallels VM.** Lima is the lightest of these but still asks the developer to manage a VM lifecycle (`limactl start`, networking, file sharing) on top of the existing toolchain. The CFI analyzer this project is building does not need a Linux VM, only an execution surface for ELFs.
- **CI-only execution (skip local run).** Loses the fast inner loop. The CFI bypass surface work that follows will require running instrumented binaries hundreds of times during development; deferring all execution to CI is not viable.
- **`brew install` a third-party qemu-user port for macOS.** No reputable maintained port exists. Building qemu-user from source against Darwin is a research project, not a class deliverable.
- **Rosetta 2 inside a Linux ARM64 VM.** Solves a different problem (running x86_64 Linux binaries on Apple Silicon Linux). Our binaries are aarch64, so Rosetta adds nothing.

## Rationale

- **Docker Desktop on Apple Silicon runs `linux/arm64` containers via the Apple Virtualization framework, not emulation.** Execution speed is essentially native for the aarch64 binary inside the container. There is no QEMU translation layer involved on macOS, which is exactly what we want.
- The **static binary** produced by ADR-001/002 means we do not need a full sysroot inside the container — `alpine:3.20` is just the kernel-userspace boundary and a working `/`. We are not depending on Alpine's libc; the binary brings its own.
- **One short Makefile change** — swap `QEMU` for a platform-aware `RUNNER`. The directory layout, the toolchain, the compile step, and every artifact path documented in ADR-001 and ADR-002 are unaffected.
- **Runner override preserves portability.** Linux hosts and CI can set `RUNNER=qemu-aarch64` and reproduce the ADR-001 path exactly.
- **No long-lived VM.** `docker run --rm` cleans up after every invocation; there is nothing to manage between runs.

## Implementation Notes

**Files to modify:**

1. `/Users/kejor/ee202c/project/baseline/aarch64-hello/Makefile`

   Replace the current `QEMU` variable and `run` target with a host-detected `RUNNER`:

   ```makefile
   # Baseline aarch64 hello-world build
   # See ADR-2026-05-25-001, ADR-2026-05-30-002, ADR-2026-05-30-003 for rationale.

   CC      ?= aarch64-unknown-linux-gnu-gcc
   CFLAGS  := -O0 -g -static -Wall -Wextra

   BUILD_DIR := build
   TARGET    := $(BUILD_DIR)/hello
   SRC       := hello.c

   # Host-aware runner selection (ADR-2026-05-30-003).
   # macOS: no qemu-user package exists; use Docker linux/arm64 (Apple Virtualization).
   # Linux: qemu-aarch64 user-mode (ADR-001).
   UNAME_S := $(shell uname -s)
   ifeq ($(UNAME_S),Darwin)
       RUNNER ?= docker run --rm --platform linux/arm64 -v $(CURDIR)/$(BUILD_DIR):/build alpine:3.20 /build/hello
   else
       RUNNER ?= qemu-aarch64 $(TARGET)
   endif

   .PHONY: all compile run clean

   all: compile

   compile: $(TARGET)

   $(TARGET): $(SRC) | $(BUILD_DIR)
   	$(CC) $(CFLAGS) -o $@ $<

   $(BUILD_DIR):
   	mkdir -p $(BUILD_DIR)

   run: compile
   	$(RUNNER); \
   	status=$$?; \
   	if [ $$status -ne 0 ]; then \
   		echo "runner exited with status $$status" >&2; \
   		exit $$status; \
   	fi

   clean:
   	rm -rf $(BUILD_DIR)
   ```

   Key points in the diff:
   - `QEMU` is removed; `RUNNER` is the new abstraction.
   - On Darwin, `RUNNER` is a full command including the path to the binary inside the container (`/build/hello`), so the `run` target invokes `$(RUNNER)` directly without appending `$(TARGET)`.
   - On Linux, `RUNNER` is `qemu-aarch64 $(TARGET)` for symmetry — same shape: a complete command.
   - `$(CURDIR)` (not `$(PWD)`) is the make-native absolute path; avoids surprises when `make -C` is used.
   - The error message is generalized from `qemu-aarch64 exited with status` to `runner exited with status` since the runner is no longer always QEMU.

2. `/Users/kejor/ee202c/project/baseline/aarch64-hello/README.md`

   - Add a "macOS execution" section documenting the Docker Desktop dependency and the `--platform linux/arm64` mechanism (Apple Virtualization framework, near-native, no emulation).
   - Note: Docker Desktop must be **running** before `make run` on macOS. A stopped Docker Desktop produces a confusing "Cannot connect to the Docker daemon" error.
   - Document the override: `RUNNER='qemu-aarch64 build/hello' make run` to use qemu-user on a Linux host or in CI.
   - Pin the Alpine image tag (`alpine:3.20`) in the README and explain why it is used (thin Linux kernel/userspace boundary; the static binary brings its own libc).

**Gotchas:**

- **Docker Desktop must be running.** The first run on a fresh checkout will also pull `alpine:3.20` (a few MB); this is a one-time cost.
- **Bind mount path.** Must use an absolute path on the host side of `-v`; relative paths silently break under some Docker Desktop configurations. `$(CURDIR)/$(BUILD_DIR)` is absolute because `$(CURDIR)` is.
- **File permissions inside the container.** `alpine:3.20` runs as root by default; the static `hello` binary is executable as built. If we ever switch to a non-root image (e.g., `distroless`), we will need to either `chmod +x` before mount or add `--user`.
- **Exit code propagation.** `docker run` returns the container's exit code, so the existing status check still works.
- **CI portability.** GitHub Actions `ubuntu-latest` runners have `qemu-user-static` available; setting `RUNNER='qemu-aarch64 build/hello'` in the CI env reproduces ADR-001's reference path. Do not hard-code Docker on the Linux path.
- **`alpine:3.20` is pinned, not `:latest`.** Pinning prevents a future Alpine update from breaking the baseline silently. Bump the tag in a follow-up ADR if/when needed.

**Security considerations:**

- **Container image is a supply-chain dependency.** `alpine:3.20` is pulled from Docker Hub. For this baseline the image is used purely as an execution shim for our own trusted binary, so the threat is limited to "a malicious Alpine could lie about the exit code or output." Mitigations: pin a specific tag (done), and treat any future use of this container with untrusted binaries as a separate ADR requiring image digest pinning (`alpine@sha256:...`) and `--read-only --network=none --cap-drop=ALL`.
- **Docker socket exposure.** `docker run` requires the developer's user to talk to the Docker Desktop daemon. This grants effective root on the host. This is a property of Docker Desktop, not of this ADR; documented for awareness.
- **Network and capabilities.** Hello-world does not need network or extra capabilities. A hardening follow-up could add `--network=none --cap-drop=ALL --read-only` to the macOS `RUNNER` default. Deferred to a later ADR to keep this change single-scoped.
- **Threat model unchanged for trusted inputs.** This ADR does not loosen the ADR-001 rule that untrusted binaries are out of scope for the baseline runner. When the CFI analyzer eventually executes adversarial samples, a separate ADR will define a hardened sandboxed runner.

## Consequences

**Enables:**

- `make run` succeeds end-to-end on macOS Apple Silicon, completing the bring-up that ADR-001 specified.
- A clean abstraction (`RUNNER`) that future `baseline/*` directories can reuse for other architectures (e.g., a `linux/amd64` runner for x86_64 baselines, a `qemu-system-*` runner for kernel-level work).
- Restores a fast local feedback loop, which the CFI bypass surface work will depend on heavily.

**Constraints:**

- Adds **Docker Desktop** as a documented macOS dependency. This is a heavyweight install (multi-GB) but is standard on developer machines and is already commonly used in IoT/security classes.
- macOS execution now depends on **Apple Virtualization framework** support, which requires macOS 13+ and an Apple Silicon or recent Intel Mac. Documented but not enforced in the Makefile.
- The Makefile's runner abstraction is platform-detected via `uname`; cross-OS edge cases (WSL, Linux-on-ARM-Mac via Asahi) will use the Linux branch, which is the correct behavior.
- ADR-001's reference to `QEMU` as the variable name is now superseded by `RUNNER`; ADR-001 remains historically accurate but the Makefile no longer matches it verbatim. Future ADRs should reference `RUNNER`.

## Acceptance Criteria

- [ ] `/Users/kejor/ee202c/project/baseline/aarch64-hello/Makefile` no longer defines a `QEMU` variable; it defines `RUNNER` with a Darwin/Linux split.
- [ ] On macOS Apple Silicon with Docker Desktop running, `cd /Users/kejor/ee202c/project/baseline/aarch64-hello && make run` prints `Hello, aarch64` and exits 0.
- [ ] On a Linux host with `qemu-user` installed, `make run` (no overrides) prints `Hello, aarch64` and exits 0.
- [ ] `RUNNER='qemu-aarch64 build/hello' make run` works on a Linux host, demonstrating the override path.
- [ ] `make compile` and `make clean` behavior is unchanged from ADR-002.
- [ ] README documents: (a) Docker Desktop as a macOS prerequisite, (b) the `--platform linux/arm64` mechanism, (c) the `alpine:3.20` pin and why, (d) the `RUNNER=` override for Linux/CI.
- [ ] No `qemu-aarch64: command not found` error on macOS for the default `make run` invocation.
