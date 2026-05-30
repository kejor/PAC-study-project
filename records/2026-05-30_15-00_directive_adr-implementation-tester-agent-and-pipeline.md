# Directive: ADR Implementation Tester Agent and Three-Stage Pipeline

**Timestamp**: 2026-05-30 15:00 (UTC-5)
**Type**: Directive
**Author/Source**: User
**Tags**: directive, workflow, agents, adr, pipeline, testing

## Summary

The user introduced a new agent, `adr-implementation-tester`, and codified a three-stage ADR delivery pipeline. Testing and bug-fixing of ADR implementations is now the exclusive responsibility of the tester agent; the security-architect and adr-implementer agents must not perform this role.

## Details

The established pipeline is:

1. **security-architect** — drafts the ADR (problem statement, alternatives, decision, consequences)
2. **adr-implementer** — implements the ADR in code and commits the result
3. **adr-implementation-tester** — runs verification targets, diagnoses failures, applies fixes, and re-commits until all checks pass

Prior to this directive, there was no dedicated tester agent. Implementation fixes (such as the corrections in commit `3468643` covering `CC =` defaults, `verify-static` removal, and SIGBUS acceptance) were handled ad hoc during the implement phase or by the general-purpose Claude session. This directive formalizes the separation of concerns.

## Rationale / Decision Basis

Separating the test-and-fix role into its own agent enforces single responsibility across the pipeline. The implementer can focus on faithfully translating the ADR into code; the tester brings adversarial verification and repair expertise without scope overlap. This prevents implementation agents from silently papering over failures that should be surfaced as distinct fix commits with clear attribution.

## Impact

- **Process constraint:** The security-architect and adr-implementer agents must hand off to `adr-implementation-tester` after implementation commits. Neither agent should self-correct implementation failures going forward.
- **Record keeping:** Fix commits produced during the test phase should be attributed to `adr-implementation-tester`, not to `adr-implementer`.
- **Enables:** Cleaner audit trail — each pipeline stage's output is attributable to exactly one agent role, making post-hoc review and blame straightforward.

## Related Records

- `records/2026-05-30_12-45_adr_pac-enabled-baseline.md`
- `records/2026-05-30_13-00_bug-fix_gnu-make-cc-default-and-verify-corrections.md`
- `records/2026-05-30_14-00_session-summary_pac-baseline-bring-up.md`
