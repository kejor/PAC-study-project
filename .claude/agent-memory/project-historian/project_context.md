---
name: project-context
description: Current project identity, class context, and active direction for EE202C
metadata:
  type: project
---

This is a new EE202C (IoT Security) class project, started fresh as of 2026-05-25.

**Prior direction abandoned**: CFI bypass surface analyzer using Unicorn emulator, Capstone disassembler, and CFB metric scoring. That project is fully abandoned — no code or analysis artifacts carry forward.

**Current direction**: aarch64 PAC (Pointer Authentication Codes) bypass surface analysis for EE202C. Both baselines are operational as of 2026-05-30: `baseline/aarch64-hello/` (no-PAC control) and `baseline/aarch64-hello-pac/` (PAC-enabled target). Next expected work: CFI bypass surface analyzer ADR targeting the PAC baseline binary.

**Why:** User explicitly reset the project scope at the start of the 2026-05-25 session.

**How to apply:** Do not reference Unicorn, Capstone, or CFB metrics as if they are part of this project. Treat every record written here as belonging to the new direction.
