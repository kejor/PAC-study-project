# PAC Vulnerability Test Suite

Nine self-contained C programs demonstrating attack primitives that PAC-ret blocks (t01–t04), two negative controls showing what PAC-ret does not protect (t05a/t05b), one documented PAC-ret bypass via SP-collision LR reuse (t06), one positive demonstration of explicit PAC data-signing protecting arbitrary 64-bit data (t07), one software PAC-oracle brute-force showing PAC's finite-entropy bound (t08), and one demonstration of LLVM's default `pac-ret` leaf-function signing gap (t09), closed by `pac-ret+leaf`. Most programs are built in two variants — no-PAC and PAC — and driven by a Python harness that checks expected outcomes; t09 adds a third `pac_leaf` variant.

See ADR-2026-05-30-005 for the original suite design, ADR-2026-05-30-006 for t06, ADR-2026-05-30-007 for t07, ADR-2026-05-30-008 for t08, and ADR-2026-05-30-009 for t09.

## Prerequisites

- `aarch64-unknown-linux-gnu-gcc` — via `brew tap messense/macos-cross-toolchains && brew install aarch64-unknown-linux-gnu`
- Docker Desktop (macOS) — must be running; `docker run --platform linux/arm64 alpine:3.20` must work
- Python 3

## Usage

```sh
# Run the full suite (all 19 cases: 8 tests x 2 variants + t09 x 3 variants)
make test

# Run a single test
make test-t01_stack_bof_ret

# Check that all pac/ and pac_leaf/ binaries contain paciasp/autiasp
# instructions (plus the t09-specific leaf-signing-gap invariant)
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

| Test | Attack primitive | No-PAC | PAC | PAC+leaf |
|---|---|---|---|---|
| t01 | Stack buffer overflow → return to `exploit_success` | `PWNED:t01`, exit 0 | Blocked (SIGSEGV) | — |
| t02 | Return to never-called `secret_admin` | `PWNED:t02`, exit 0 | Blocked (SIGSEGV) | — |
| t03 | 2-gadget ROP chain | `PWNED:t03`, exit 0 | Blocked (SIGSEGV) | — |
| t04 | Off-by-N LR low-byte corruption (3 bytes, 16 MiB window) | `PWNED:t04`, exit 0 | Blocked (SIGSEGV) | — |
| t05a | Function-pointer overwrite (forward-edge) | `PWNED:t05a`, exit 0 | `PWNED:t05a`, exit 0 | — |
| t05b | Data-only (`is_admin` flag) | `PWNED:t05b`, exit 0 | `PWNED:t05b`, exit 0 | — |
| t06  | SP-collision PAC reuse (signed LR replay across peers) | `PWNED:t06`, exit 0 | `PWNED:t06`, exit 0 | — |
| t07  | Explicit `pacia`/`autia` data signing of a critical config word | `PWNED:t07`, exit 0 | Blocked (SIGSEGV) | — |
| t08  | Software PAC oracle: SIGSEGV/SIGBUS/SIGILL handler + sigsetjmp/siglongjmp brute-forces the low 8 bits of a `pacia`-signed function pointer, then calls it via `blraa` | `PWNED:t08`, exit 0 | `PWNED:t08`, exit 0 | — |
| t09  | Leaf-function X30 corruption via inline-asm `mov x30, %0` (LLVM's default `pac-ret` leaves leaf functions unsigned) | `PWNED:t09`, exit 0 | `PWNED:t09`, exit 0 (gap) | Blocked (SIGSEGV) under `pac-ret+leaf` |

t05a and t05b are **negative controls** — PAC-ret only signs/verifies return addresses, so forward-edge and data-only attacks are outside its threat model. Both variants succeeding is the expected, correct result.

t06 is a **documented PAC-ret bypass** (Liljestrand et al., USENIX Sec '19, Sections 3 / 7.2.1): pac-ret uses SP alone as the signing modifier, so an authenticated LR captured from one function is reusable inside any peer function called at the same SP. Both variants succeeding is the expected result — it is the one case in this suite where `-mbranch-protection=pac-ret` does not block a return-address primitive.

t07 is a **positive complement to t05b** (Choi et al., ICTC 2025, "I-ACIV"): PAC hardware CAN protect arbitrary 64-bit data — but only if the program explicitly invokes `pacia`/`autia` on that data. Unlike t01–t06, t07's no-PAC vs PAC split is a source-level preprocessor toggle (`-DUSE_PAC_DATA_SIGNING=1` on the PAC variant) rather than `-mbranch-protection`, because `pac-ret` does not emit data-signing instructions. The PAC variant traps on the corrupted authenticated value; the no-PAC variant runs the same source with the inline asm `#if`'d out and reaches the win target. Requires ARMv8.3-A PAC at runtime (Apple Silicon via Docker linux/arm64 is sufficient; on hardware without v8.3 PAC, `pacia`/`autia` execute as HINT NOPs and the PAC variant would fail).

t08 is a **software PAC-oracle demonstration** (Ravichandran et al., "PACMAN", IEEE Computer Architecture Top Picks 2022): PAC's MAC is finite-entropy and brute-forceable by anyone holding a crash/no-crash distinguisher. t08 builds that distinguisher in pure POSIX — a SIGSEGV/SIGBUS/SIGILL handler paired with `sigsetjmp`/`siglongjmp` — and uses it to enumerate the low 8 bits (`BRUTE_BITS=8`, 256 iterations) of a `pacia`-signed function pointer, then calls the recovered pointer via `blraa`. PACMAN's contribution is making this oracle stealthy via Apple-M1 speculative execution; t08 demonstrates the underlying logical primitive without speculation. Both variants are expected to PWN (t08 joins t06 as the second case where the PAC variant succeeds — t06 is a primitive-class weakness, t08 is an entropy-class weakness). Like t07, the PAC variant uses a source-level toggle (`-DUSE_PAC_BRUTE=1`) and requires ARMv8.3-A PAC at runtime. The brute-force scope is a runtime-budget choice; rebuilding with `-DBRUTE_BITS=24` enumerates the full PAC field (~17 s per the PACMAN paper). See `t08_pac_oracle_bruteforce/vuln.c` for the source header explaining the trade-offs.

t09 is a **leaf-function signing-gap demonstration**. LLVM's AArch64 backend defines `enum class SignReturnAddress { None, NonLeaf, All };` in `arm-toolchain/llvm/lib/Target/AArch64/AArch64MachineFunctionInfo.h`. The default for `-mbranch-protection=pac-ret` is `SignReturnAddress::NonLeaf`; the helper `shouldSignReturnAddress(Cond, IsLeaf)` returns `false` whenever `Cond == NonLeaf && IsLeaf == true`. In `arm-toolchain/llvm/lib/Target/AArch64/AArch64PointerAuth.cpp`, `signLR()` and the `PAUTH_PROLOGUE` / `PAUTH_EPILOGUE` pseudos are gated on that helper, so a leaf function under `pac-ret` emits *no* `paciasp` and *no* `autiasp` — its `ret` is an unauthenticated indirect branch on whatever lives in X30. The mitigation is `-mbranch-protection=pac-ret+leaf`, which selects `SignReturnAddress::All` and signs every function. t09 corrupts X30 directly via inline asm `mov x30, %0` inside a `noinline` leaf and observes: `nopac` PWNs (no PAC at all), `pac` PWNs (the gap — LLVM's default policy leaves leaves unsigned), `pac_leaf` traps (`autiasp` poisons the corrupted X30, the subsequent `ret` faults). t09 is the first test in the suite to introduce a third build variant; the per-test variant model in the Makefile (`VARIANTS_<test>`) generalizes to any future test that needs a richer configuration matrix. The same ARMv8.3-A hardware caveat as t07 and t08 applies to the `pac_leaf` variant; `make verify-static` additionally asserts at the static level that `paciasp` is *absent* from `pac/t09`'s `leaf_victim` (gap) and *present* in `pac_leaf/t09`'s `leaf_victim` (mitigation).

## Key implementation constraints

- `-fno-stack-protector` is mandatory on all builds: the stack canary would fire before PAC on t01–t04, masking the PAC signal.
- Payloads use `__builtin_frame_address(0)` for runtime LR offset discovery — no hand-counted offsets.
- t01–t04 use a two-function structure (`victim` + `inner_victim`) because AArch64 GCC `-O0` places locals at positive fp offsets, so the buffer lives in `inner_victim` and the overflow corrupts `victim`'s saved LR.
