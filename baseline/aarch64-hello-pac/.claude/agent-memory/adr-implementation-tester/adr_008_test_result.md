---
name: adr-008-test-result
description: PAC Oracle brute-force test (t08): all 18 suite cases PASS on first run, no fixes needed, tested 2026-05-31
metadata:
  type: project
---

ADR-2026-05-30-008 (t08_pac_oracle_bruteforce) was tested at commit afe7885. All 18 test cases (16 prior + 2 new t08 variants) PASS on the first run with no investigative changes required.

Key observations:
- `make test` exits 0, all 18 PASS.
- `make test-t08_pac_oracle_bruteforce` exits 0, both variants PASS.
- `make verify-static` passes for all 9 pac binaries; t08 pac binary shows paciasp/pacia=15, autiasp/autia=1.
- objdump confirms pac binary has 15 pacia and 1 blraa mnemonic; nopac binary has 0 pacia and 0 blraa.
- Two documented deviations from ADR's prescribed vuln.c both present and correct: (1) PAC-mask discovery unions XORs across 33 modifiers (not single XOR); (2) brute_width = found (adapts to actual PAC bit width, currently ~7 bits in Apple Virtualization Framework / Docker).
- Prior t01-t07 tests unaffected.

**Why:** Runtime environment on Apple Virtualization Framework exposes only ~7 PAC bits for low-address user pointers, not the textbook 16-24 — the adaptive brute_width handles this correctly.
**How to apply:** The suite now has two entries where the pac variant is expected PWNED: t06 (SP collision) and t08 (oracle brute-force).
