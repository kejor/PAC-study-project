# PAC Vulnerability Test Suite

Five self-contained C programs demonstrating attack primitives that PAC-ret blocks (t01–t04) and two negative controls showing what PAC-ret does not protect (t05). Each program is built in two variants — no-PAC and PAC — and driven by a Python harness that checks expected outcomes.

See ADR-2026-05-30-005 for full design rationale.

## Prerequisites

- `aarch64-unknown-linux-gnu-gcc` — via `brew tap messense/macos-cross-toolchains && brew install aarch64-unknown-linux-gnu`
- Docker Desktop (macOS) — must be running; `docker run --platform linux/arm64 alpine:3.20` must work
- Python 3

## Usage

```sh
# Run the full suite (all 12 cases)
make test

# Run a single test
make test-t01_stack_bof_ret

# Check that all PAC binaries contain paciasp/autiasp instructions
make verify-static

# Build without running
make build

# Clean build artifacts
make clean
```

> **macOS:** Always use `make test`, not `python3 harness/run_tests.py` directly.
> The Makefile exports `DOCKER_RUN` which the harness needs to invoke the
> Docker runner. Running the harness directly will fail with
> `Exec format error` because the aarch64 binaries are not natively
> executable on macOS.

## Test matrix

| Test | Attack primitive | No-PAC | PAC |
|---|---|---|---|
| t01 | Stack buffer overflow → return to `exploit_success` | `PWNED:t01`, exit 0 | Blocked (SIGSEGV) |
| t02 | Return to never-called `secret_admin` | `PWNED:t02`, exit 0 | Blocked (SIGSEGV) |
| t03 | 2-gadget ROP chain | `PWNED:t03`, exit 0 | Blocked (SIGSEGV) |
| t04 | Off-by-N LR low-byte corruption (3 bytes, 16 MiB window) | `PWNED:t04`, exit 0 | Blocked (SIGSEGV) |
| t05a | Function-pointer overwrite (forward-edge) | `PWNED:t05a`, exit 0 | `PWNED:t05a`, exit 0 |
| t05b | Data-only (`is_admin` flag) | `PWNED:t05b`, exit 0 | `PWNED:t05b`, exit 0 |

t05a and t05b are **negative controls** — PAC-ret only signs/verifies return addresses, so forward-edge and data-only attacks are outside its threat model. Both variants succeeding is the expected, correct result.

## Key implementation constraints

- `-fno-stack-protector` is mandatory on all builds: the stack canary would fire before PAC on t01–t04, masking the PAC signal.
- Payloads use `__builtin_frame_address(0)` for runtime LR offset discovery — no hand-counted offsets.
- t01–t04 use a two-function structure (`victim` + `inner_victim`) because AArch64 GCC `-O0` places locals at positive fp offsets, so the buffer lives in `inner_victim` and the overflow corrupts `victim`'s saved LR.
