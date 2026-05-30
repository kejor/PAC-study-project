# aarch64 Hello World Baseline

A statically-linked aarch64 Linux ELF hello-world binary. On Linux hosts it is
executed via QEMU user-mode emulation; on macOS hosts it is executed inside a
Docker `linux/arm64` container backed by the Apple Virtualization framework
(no QEMU user-mode is available on Darwin). See
[ADR-2026-05-25-001](../../adr/ADR-2026-05-25-001-aarch64-qemu-hello-world.md),
[ADR-2026-05-30-002](../../adr/ADR-2026-05-30-002-macos-toolchain-and-makefile-fix.md),
and [ADR-2026-05-30-003](../../adr/ADR-2026-05-30-003-macos-qemu-user-mode-workaround.md)
for design rationale.

## Prerequisites (macOS)

Install the aarch64 GCC cross toolchain and Docker Desktop:

```sh
brew tap messense/macos-cross-toolchains
brew install aarch64-unknown-linux-gnu
brew install --cask docker
```

The cross compiler is exposed as `aarch64-unknown-linux-gnu-gcc`, which is the
Makefile default (see [ADR-2026-05-30-002](../../adr/ADR-2026-05-30-002-macos-toolchain-and-makefile-fix.md)).
On Linux hosts that ship the `aarch64-linux-gnu-gcc` triple instead, override
`CC` on the command line:

```sh
make compile CC=aarch64-linux-gnu-gcc
```

### macOS execution backend

Homebrew's `qemu` formula does not ship `qemu-aarch64` (user-mode) on macOS,
because QEMU user-mode forwards Linux syscalls to a Linux host kernel and that
ABI does not exist on Darwin. ADR-2026-05-30-003 selects Docker Desktop with
`--platform linux/arm64` as the macOS execution path: on Apple Silicon, Docker
Desktop runs `linux/arm64` containers via the Apple Virtualization framework
(no software emulation), so the static aarch64 ELF runs at near-native speed.

The Makefile auto-detects Darwin and invokes:

```
docker run --rm --platform linux/arm64 -v $(CURDIR)/build:/build alpine:3.20 /build/hello
```

**Docker Desktop must be running** before `make run` on macOS. A stopped
Docker Desktop produces a confusing "Cannot connect to the Docker daemon"
error. The first invocation will also pull `alpine:3.20` (a few MB); this is a
one-time cost.

The `alpine:3.20` tag is pinned (not `:latest`) to prevent a future Alpine
update from breaking the baseline silently. Alpine is used purely as a thin
Linux kernel/userspace boundary — the static binary brings its own libc, so we
do not depend on Alpine's musl. Bumping the tag is a follow-up ADR.

### Linux / CI execution backend

On Linux hosts the Makefile defaults to `qemu-aarch64 build/hello`, matching
ADR-2026-05-25-001. Install `qemu-user` (or `qemu-user-static` on CI):

```sh
sudo apt-get install qemu-user-static   # Debian/Ubuntu
```

### Overriding the runner

The `RUNNER` variable can be overridden to force a specific backend. For
example, on a Linux host that happens to have Docker but not `qemu-user`, or
in CI where you want to pin the QEMU path explicitly:

```sh
RUNNER='qemu-aarch64 build/hello' make run
```

The override accepts any complete command; the Makefile invokes `$(RUNNER)`
directly and does not append `$(TARGET)`.

### Toolchain versions used during initial bring-up

- Host: macOS (darwin), 2026-05-25
- Cross compiler: `aarch64-unknown-linux-gnu-gcc` 13.x (Homebrew
  `messense/macos-cross-toolchains/aarch64-unknown-linux-gnu`)
- macOS runtime: Docker Desktop with `--platform linux/arm64` on
  `alpine:3.20`
- Linux runtime: `qemu-aarch64` 8.x (from distro `qemu-user` / `qemu-user-static`)

Record the exact versions you ran with:

```sh
$(CC) --version
docker --version          # macOS
qemu-aarch64 --version    # Linux
```

## Build

```sh
make compile
```

Produces `build/hello`. `make` (with no target) does the same via the default
`all` goal.

## Run

```sh
make run
```

Expected output:

```
Hello, aarch64
```

Exit status: `0`.

## Verify the binary format

```sh
file build/hello
```

Should report something like:

```
build/hello: ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux),
statically linked, ...
```

## Clean

```sh
make clean
```

Removes the `build/` directory.
