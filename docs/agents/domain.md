# Domain Docs

This is a single-context repo whose domain language and architecture are upstream-derived. Engineering skills should use the repo-level domain and architecture documentation before making context-sensitive changes, then verify against current upstream sources when details may have drifted.

## Before exploring, read these

- `AGENTS.md` for current repo guidance, critical invariants, build commands, and test commands.
- `CONTEXT.md` at the repo root for upstream-derived Lemonade vocabulary, relationships, and flagged ambiguities.
- `docs/agents/fork-stewardship.md` for fork-local operating policy.
- `docs/agents/research-map.md` for source maps, scout routes, and common false assumptions.
- `docs/agents/architecture-map.md` for compact implementation landmarks and architecture guardrails.
- `docs/adr/` for architectural decisions that touch the area being changed, when present.
- Existing docs under `docs/` for user-facing, developer-facing, API, integration, and platform-specific behavior.

If `docs/adr/` does not exist, proceed silently. Do not flag the absence or suggest creating it upfront. Producer skills create ADRs when architectural decisions need a durable home.

## File structure

Expected single-context layout:

```text
/
├── AGENTS.md
├── CONTEXT.md
├── docs/
│   ├── agents/
│   ├── adr/
│   ├── api/
│   ├── dev/
│   ├── guide/
│   └── integrations/
└── src/
```

## Use project vocabulary

When output names a domain concept in an issue title, refactor proposal, hypothesis, or test name, use the term as defined by the repo docs. Avoid introducing alternate names for existing concepts.

If the needed concept is not documented yet, inspect upstream docs and source first. Add stable, upstream-derived terminology to `CONTEXT.md`; note uncertain or fork-local terminology as an open gap instead of canonizing it.

## Flag ADR conflicts

If output contradicts an existing ADR, surface the conflict explicitly and identify the ADR.
