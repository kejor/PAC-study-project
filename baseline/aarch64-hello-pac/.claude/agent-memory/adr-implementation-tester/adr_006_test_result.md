---
name: adr-006-test-result
description: ADR-2026-05-30-006 SP-collision PAC reuse test (t06): all 14 suite cases PASS on first run, no fixes needed, tested 2026-05-30
metadata:
  type: project
---

Tested ADR-2026-05-30-006 implementation on 2026-05-30. All acceptance criteria met on first run; no code changes were required.

**Results summary:**
- `make test` (all 14 cases): ALL PASS — t01–t05 unchanged, t06 nopac PASS, t06 pac PASS
- `make test-t06_sp_collision_pac_reuse`: PASS both variants
- `make verify-static`: OK for all 7 pac binaries (t06 pac: paciasp=11, autiasp=2)
- Docker direct execution of t06 nopac: `PWNED:t06`, exit 0
- Docker direct execution of t06 pac: `PWNED:t06`, exit 0 — no SIGSEGV/SIGBUS, SP-collision bypass confirmed working

**Why:** SP-equality precondition held: donor and victim both called from same depth in main with identical local variable sets; `__builtin_frame_address(0)` matched in both, paciasp/autiasp modifiers aligned, SP-collision bypass demonstrated.

**How to apply:** Future ADR tests for t06 do not need re-debugging; the frame-layout strategy (identical locals declared in exact same order in both donor and victim) is validated for this toolchain version.
