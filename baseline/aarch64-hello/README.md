# aarch64 Hello World Baseline

A statically-linked aarch64 Linux ELF hello-world binary, executed on macOS hosts
via QEMU user-mode emulation. See
[ADR-2026-05-25-001](../../adr/ADR-2026-05-25-001-aarch64-qemu-hello-world.md)
for design rationale.

## Prerequisites (macOS)

Install the aarch64 GCC cross toolchain and QEMU user-mode:

```sh
brew tap messense/macos-cross-toolchains
brew install aarch64-unknown-linux-gnu
brew install qemu
```

The cross compiler is exposed as `aarch64-unknown-linux-gnu-gcc`, which is the
Makefile default (see [ADR-2026-05-30-002](../../adr/ADR-2026-05-30-002-macos-toolchain-and-makefile-fix.md)).
On Linux hosts that ship the `aarch64-linux-gnu-gcc` triple instead, override
`CC` on the command line:

```sh
make compile CC=aarch64-linux-gnu-gcc
```

`qemu-aarch64` ships inside the `qemu` formula.

### Toolchain versions used during initial bring-up

- Host: macOS (darwin), 2026-05-25
- Cross compiler: `aarch64-unknown-linux-gnu-gcc` 13.x (Homebrew
  `messense/macos-cross-toolchains/aarch64-unknown-linux-gnu`)
- Emulator: `qemu-aarch64` 8.x (Homebrew `qemu`)

Record the exact versions you ran with:

```sh
$(CC) --version
qemu-aarch64 --version
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
