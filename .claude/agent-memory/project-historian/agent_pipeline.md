---
name: agent-pipeline
description: Three-stage ADR delivery pipeline: security-architect → adr-implementer → adr-implementation-tester; Claude must not test or fix ADR implementations itself
metadata:
  type: feedback
---

The ADR delivery pipeline has three exclusive stages:
1. **security-architect** — drafts the ADR
2. **adr-implementer** — implements in code
3. **adr-implementation-tester** — runs verification, diagnoses failures, applies fixes

**Why:** Directive issued 2026-05-30 to enforce single responsibility and a clean audit trail. Prior to this, fix commits (e.g., `3468643`) were applied without clear agent attribution. The tester agent was created specifically to own this role.

**How to apply:** Never test or fix ADR implementations as security-architect or adr-implementer. If a verification failure is discovered post-implementation, hand off to `adr-implementation-tester`. Fix commits in records should be attributed to that agent, not to the implementer.

[[project-context]]
