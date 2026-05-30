---
name: project-structure
description: Directory layout, file naming conventions, and ADR conventions for the EE202C project
metadata:
  type: project
---

**Directory layout** (as of 2026-05-25):

```
adr/            Architecture Decision Records
baseline/       Cross-compiled binary inputs and test programs
records/        Project historian session/event records (this historian's output)
.gitignore      Excludes baseline/*/build/ — compiled ELFs not committed
```

**ADR naming convention**: `adr/ADR-YYYY-MM-DD-NNN-slug.md` (lowercase slug, zero-padded sequence number)
- Example: `adr/ADR-2026-05-25-001-aarch64-qemu-hello-world.md`
- ADR status field progresses: `Ready` → `Implemented` (updated in place on implementation)

**Baseline binary layout**: `baseline/<target-name>/` contains `hello.c`, `Makefile`, `README.md`; build output goes to `baseline/<target-name>/build/` (gitignored)

**Records naming convention**: `records/YYYY-MM-DD_HH-MM_<event-type>_<short-slug>.md`

**Why:** Established during the first real session (2026-05-25) when ADR-001 was drafted and implemented.

**How to apply:** Use these conventions when naming new record files and when referencing project files in record entries. [[project-context]]
