---
name: adr-007-test-result
description: ADR-2026-05-30-007 explicit data signing test (t07): all 16 suite cases PASS on first run, no fixes needed, tested 2026-05-31
metadata:
  type: project
---

ADR-2026-05-30-007 (`t07_explicit_data_signing`) passed all acceptance criteria on first run. No investigative changes were needed.

**Why:** t07 adds explicit `pacia`/`autia` inline-asm PAC data signing as the complement to t05b. The variant split uses a preprocessor flag (`-DUSE_PAC_DATA_SIGNING=1`) rather than the `-mbranch-protection` flag used by t01-t06. `-march=armv8.3-a` was also needed and is documented in the ADR implementation notes.

**How to apply:** Future t07-adjacent tests (e.g. `pacda`/`autda` variant) should follow the same source-level toggle pattern and also add `-march=armv8.3-a` to their pac CFLAGS.

**Test run summary (2026-05-31):**
- `make build`: 16 binaries compiled cleanly, no warnings.
- `make verify-static`: all 8 pac binaries OK; t07 pac shows pacia=11, autia=2 (inline-asm `pacia`/`autia` + compiler-emitted `paciasp` from pac-ret).
- `make test`: 16/16 PASS (t01-t06 unaffected, t07 both variants pass).
- `make test-t07_explicit_data_signing`: 2/2 PASS in isolation.
- t07 nopac: exit 0, stdout="PWNED:t07".
- t07 pac: exit 139 (SIGSEGV), stdout empty (no PWNED:t07).
- nopac binary: zero `pacia`/`autia` instructions (confirmed by objdump grep returning empty).

Related: [[adr-005-test-result]], [[adr-006-test-result]]
