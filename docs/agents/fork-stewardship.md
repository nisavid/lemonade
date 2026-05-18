# Fork Stewardship

This repo is a fork of upstream Lemonade. Agents should treat upstream as the source of product truth and this fork as the default destination for local work.

## Current Policy

- Upstream project: `lemonade-sdk/lemonade`
- Fork origin: `nisavid/lemonade`
- Product site: <https://lemonade-server.ai/>
- Upstream docs: <https://lemonade-server.ai/docs/>
- Default change target: fork origin only

Do not open upstream pull requests, create upstream issues, or shape work as upstream-bound unless the user explicitly says upstream is in scope.

## Authority Order

Use this order when sources disagree:

1. Explicit user direction for this fork.
2. Current upstream source, website, docs, release notes, and maintainer-authored contribution guidance.
3. Fork-local `AGENTS.md`, `CONTEXT.md`, and `docs/agents/`.
4. Inferences from source structure and tests.
5. Prior agent notes, only after checking that they still match current upstream and local files.

When an inference matters, label it as an inference. Do not present inferred architecture intent as upstream-stated fact.

## Before Specs, Plans, or Implementation

For any non-trivial change:

1. Read `AGENTS.md`, `CONTEXT.md`, `docs/agents/domain.md`, and `docs/agents/research-map.md`.
2. Identify the relevant upstream docs and source paths.
3. Verify current upstream state when drift could affect the task.
4. Record durable discoveries in the narrowest useful place:
   - `CONTEXT.md` for stable vocabulary and relationships.
   - `docs/agents/research-map.md` for source maps and scout routes.
   - `docs/agents/fork-stewardship.md` for fork operating policy.
   - `AGENTS.md` for always-loaded, high-impact rules.

Prefer short entries that tell future agents where to look and which false assumptions to avoid.

## Fork Sync and Upstream History

Preserve upstream commit identity when bringing upstream changes into this fork. Patch-equivalent content is not enough if upstream ancestry is lost.

### Upstream tracking refs

- `upstream` fetches from `https://github.com/lemonade-sdk/lemonade.git`; its push URL is disabled.
- `upstream-main` tracks `upstream/main` and is mirrored to `origin/upstream-main` for proactive upstream scouting.
- `origin/upstream-stable` marks this fork's chosen stable upstream release baseline; local `upstream-stable` tracks it for baseline maintenance.

Use `.agents/skills/working-with-upstream-refs/SKILL.md` for upstream commit work, upstream release tags, branch comparisons, fork sync baselines, or repo-specific inputs to `syncing-forks-with-upstream`.

For stable-baseline sync work:

```bash
git fetch origin
git merge-base --is-ancestor origin/upstream-stable HEAD
```

When using `syncing-forks-with-upstream` in this repo, `origin/upstream-stable` is the default upstream baseline. Use `upstream/main` only when the user explicitly asks for unreleased upstream `main`.

For user-requested upstream `main` sync work:

```bash
git merge-base --is-ancestor upstream/main HEAD
```

Avoid rebase, force-push, `gh repo sync --force`, and other history-replacing flows unless the user explicitly requests that behavior.

## Documentation Shape

Keep documentation progressive:

- Put always-needed operating rules in `AGENTS.md`.
- Put vocabulary in `CONTEXT.md`.
- Put source maps and research routes in `docs/agents/research-map.md`.
- Link to upstream docs instead of copying them.
- Capture likely mistakes and non-obvious source relationships.

Do not create large fork-local rewrites of upstream user docs unless the task is explicitly to author fork-specific user documentation.

## Role Boundary

The fork owner supplies intent, taste, UX feedback, pragmatic judgment, engineering management, and architectural discretion. Agents are responsible for source reading, architecture inference, validation, and making implementation-ready artifacts that respect upstream intent.
