---
name: project-context
description: Current project identity, class context, and active direction for EE202C
metadata:
  type: project
---

This is a new EE202C (IoT Security) class project, started fresh as of 2026-05-25.

**Prior direction abandoned**: CFI bypass surface analyzer using Unicorn emulator, Capstone disassembler, and CFB metric scoring. That project is fully abandoned — no code or analysis artifacts carry forward.

**Current direction**: aarch64 PAC (Pointer Authentication Codes) bypass surface analysis for EE202C. The PAC vulnerability test suite is complete as of 2026-05-31: 9 test cases (t01–t09), 21 binaries, all passing. Both baselines and the full test suite are operational.

**Bypass surface taxonomy (established 2026-05-31):**
- Category A (t01–t04, t07): PAC works correctly — toolchain is doing its job
- Category B (t05a/t05b): Outside pac-ret's stated threat model (forward-edge / data-only)
- Category C (t06, t08, t09): Architectural limitations or compiler policy gaps — fixes require ARMv8.6+ hardware (FPAC, PAuth2) or opt-in flags (pac-ret+leaf)

**PACMAN paper (Ravichandran et al., ISCA 2022):** Analyzed — no new test cases possible. PACMAN extends t08's entropy/oracle class via speculative execution, but requires Apple M1-specific cache gadgets, privileged cache flush instructions, and timing precision incompatible with Docker/QEMU.

**Why:** User explicitly reset the project scope at the start of the 2026-05-25 session.

**How to apply:** Do not reference Unicorn, Capstone, or CFB metrics as if they are part of this project. Treat every record written here as belonging to the new direction. The test suite is complete — new ADRs would require a new paper or toolchain source finding.
