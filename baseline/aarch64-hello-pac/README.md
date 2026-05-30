# PAC-Enabled aarch64 Hello World Baseline

A statically-linked aarch64 Linux ELF hello-world binary compiled with
ARMv8.3 **Pointer Authentication Code (PAC)** return-address signing
enabled. Identical source to the no-PAC baseline at
[`../aarch64-hello/`](../aarch64-hello/); only the build flags differ.

See [ADR-2026-05-30-004](../../adr/ADR-2026-05-30-004-pac-enabled-baseline.md)
for design rationale. This directory is the PAC-enabled sibling of the
no-PAC control sample at `baseline/aarch64-hello/` (ADRs 001-003).

## What this baseline adds over the no-PAC baseline

- `-mbranch-protection=pac-ret` is passed to gcc. Non-leaf function
  prologues get `paciasp` (sign LR with the A-key) and epilogues get
  `autiasp` (authenticate LR before `ret`). The PAC bit is also recorded
  in `.note.gnu.property` (`GNU_PROPERTY_AARCH64_FEATURE_1_AND`).
- A second program, `hello_corrupt`, deliberately overwrites the signed
  LR on the stack after `paciasp` and before `autiasp`. On a
  PAC-enforcing runtime, returning from the corrupted function traps
  with `SIGILL` (exit 132) or `SIGSEGV` (exit 139). This is the only
  check that distinguishes "PAC compiled in" from "PAC actually
  enforced at runtime."
- Two new make targets, `verify-static` and `verify-trap`, formalize
  those two checks.

## Why `pac-ret` and not `standard` (PAC+BTI)

ADR-2026-05-30-004 explicitly scopes this baseline to `pac-ret` only.
BTI requires every indirect branch target — including in `crt1.o` /
`libc.a` from the static glibc we link against — to begin with a `bti`
landing pad. The `messense/macos-cross-toolchains` glibc at time of
writing is not BTI-marked, so mixing a BTI-marked `main` with non-BTI
runtime can produce subtle behavior. PAC-ret has no whole-program
requirement; it is a pure per-function prologue/epilogue change. A
follow-up ADR will add BTI once `pac-ret` is verified working.

## Prerequisites

Same as the no-PAC baseline (see [`../aarch64-hello/README.md`](../aarch64-hello/README.md)):
the `messense/macos-cross-toolchains` aarch64 cross-toolchain plus
Docker Desktop on macOS or `qemu-user` on Linux.

```sh
brew tap messense/macos-cross-toolchains
brew install aarch64-unknown-linux-gnu
brew install --cask docker
```

The Makefile additionally invokes `aarch64-unknown-linux-gnu-readelf`
and `aarch64-unknown-linux-gnu-objdump` for the static verification
target; both ship with the same toolchain package.

### Runner must expose PAC

The macOS Docker path uses Apple's Virtualization framework, which
presents the underlying M-series CPU's armv8.3 ISA to the Linux guest;
`/proc/cpuinfo` inside the container will list `paca pacg` in
`Features:`. This is what allows `verify-trap` to observe a real PAC
authentication failure.

`qemu-aarch64` user-mode emulation on a Linux host **does not faithfully
enforce PAC** on hosts whose kernel does not advertise PAC: PAC
instructions are translated as hints/NOPs and authentication failures
will not trap. `verify-trap` will therefore fail or warn on such hosts;
this is expected, and is documented in ADR-2026-05-30-004 as a known
caveat of the Linux execution path.

## Build

```sh
make compile
```

Produces `build/hello` and `build/hello_corrupt`, both statically-linked
aarch64 ELFs. `make` with no target invokes the default `all` goal,
which is `compile`.

## Run

```sh
make run
```

Expected output:

```
Hello, aarch64
```

Exit status: `0`. The PAC-signed binary runs identically to the no-PAC
baseline on PAC-capable and pre-armv8.3 hardware alike — PAC
instructions are hint-space encodings and become NOPs on older CPUs.
That is precisely why `verify-trap` is mandatory in addition to
`verify-static`.

## Verify

```sh
make verify           # runs both verify-static and verify-trap
make verify-static    # PAC marker in .note.gnu.property + paciasp/autiasp present
make verify-trap      # build hello_corrupt, run it, assert SIGILL/SIGSEGV
```

- `verify-static` inspects the ELF: it greps `readelf -n` output for
  the AArch64 PAC feature bit in `.note.gnu.property`, and greps
  `objdump -d` for `paciasp`/`autiasp` instructions in the disassembly.
- `verify-trap` runs `hello_corrupt` through the same `RUNNER` as
  `make run`. Exit codes 132 (`SIGILL`) or 139 (`SIGSEGV`) are
  accepted as success. Exit 0 means PAC was not enforced and is a
  hard failure.

## `hello_corrupt.c` and the verified LR offset

`hello_corrupt.c` clobbers `[sp, #8]` inside the non-leaf function
`victim()`. With `aarch64-unknown-linux-gnu-gcc` 13.x at `-O0`, the
prologue of `victim` after `paciasp` is:

```
paciasp
stp     x29, x30, [sp, #-16]!
mov     x29, sp
```

This places the signed LR in the high half of the 16-byte saved
register pair, i.e. at `[sp, #8]`. The clobber `str x9, [sp, #8]`
overwrites that slot, so when the epilogue runs `autiasp` the
authentication input does not match the signature and the subsequent
`ret` traps.

If a toolchain upgrade changes the prologue (e.g. a different spill
layout or omission of the frame pointer), the offset must be re-pinned.
The bring-up procedure is:

```sh
make compile
aarch64-unknown-linux-gnu-objdump -d build/hello_corrupt \
    | sed -n '/<victim>:/,/^$/p'
```

and update both the inline `asm` in `hello_corrupt.c` and the source
comment to match the new offset.

## Host environment captured during initial bring-up

Record the exact host versions used the first time `make verify`
passes on a given developer machine. This makes future regressions
diagnosable.

```sh
$(CC) --version           # aarch64-unknown-linux-gnu-gcc
docker --version          # macOS
docker info | grep -i kernel
uname -a                  # host
docker run --rm --platform linux/arm64 alpine:3.20 uname -a
docker run --rm --platform linux/arm64 alpine:3.20 \
    sh -c 'grep -E "^Features" /proc/cpuinfo | head -1'
```

The last command should list `paca pacg` among the CPU features.
Without those, `verify-trap` cannot succeed.

| Component | Version captured at bring-up |
|---|---|
| Host OS | (fill in: e.g., macOS 14.5, Apple Silicon) |
| Docker Desktop | (fill in: e.g., 4.x) |
| Container kernel | (fill in: `uname -a` inside `alpine:3.20`) |
| Cross compiler | `aarch64-unknown-linux-gnu-gcc` 13.x |
| `pac-ret` flag | `-mbranch-protection=pac-ret` |

## Verify the binary format

```sh
file build/hello
file build/hello_corrupt
```

Both should report:

```
ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux),
statically linked, ...
```

## Clean

```sh
make clean
```

Removes the `build/` directory.

## Caveats

- **glibc in the static link is not PAC-signed.** The
  `messense/macos-cross-toolchains` glibc was not (at time of writing)
  compiled with `-mbranch-protection`, so only *our* code's returns are
  signed. For the CFI bypass surface analyzer this is acceptable and
  realistic.
- **`-O0` is load-bearing.** Higher optimization can inline `victim`
  and elide the saved-LR spill that `hello_corrupt.c` targets. Do not
  raise the optimization level for this baseline.
- **The no-PAC baseline at `../aarch64-hello/` is the control sample**
  and must not be modified by changes in this directory. ADRs 001-003
  reference it as the immutable no-hardening reference.
