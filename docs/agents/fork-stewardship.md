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

This fork preserves legacy `LEMONADE_*` configuration environment migration as
an active compatibility contract. Keep `ConfigFile::migrate_from_env()` and
`test/server_env_vars.py` working when syncing upstream release changes around
runtime configuration.

Avoid rebase, force-push, `gh repo sync --force`, and other history-replacing flows unless the user explicitly requests that behavior.

### Deferring review findings in sync PRs

When you defer a review finding on unchanged upstream code in an upstream-sync pull request, say it is out of scope for the sync and tracked as a `nisavid/lemonade` follow-up. Never say it "belongs upstream" or call it an "upstream follow-up". When you ask a review bot to file the issue, ask for it in `nisavid/lemonade` with no upstream-coordination steps. Report upstream only when the fork owner asks.

## Fork CI Guards

- Every job that publishes, pushes branches, opens pull requests, or reaches into another repository carries a job-level `github.repository == 'lemonade-sdk/lemonade'` guard, alone or ANDed into its existing `if`. Guard each such job that an upstream sync brings in.
- Tags are never pushed to the fork, and a tag ruleset enforces it (#180).
- A job may stay unguarded when a guarded job it needs keeps it from running on the fork: Launchpad PPA's `package-arm64` and `upgrade-test` sit behind `prepare-matrix`, and `benchmark-regression.yml`'s `bench` sits behind `setup`.
- Self-hosted `lemon-prod` jobs that only test, validate or check runner health stay unguarded on purpose: `cpp_server_build_test_release.yml`'s `test-exe-inference` and `test-deb-inference`, the `validate` jobs in `validate_llamacpp.yml`, `validate_sdcpp.yml` and `validate_vllm.yml`, and `runner_heartbeat.yml`. On the fork they queue until GitHub cancels them, which is an accepted CI exception.
- `linux_distro_builds.yml` is a fork divergence. Upstream deleted it in lemonade-sdk/lemonade#3524; the fork keeps an Arch-only build as a blocking part of its CI bar. Rerun it, or record a waiver, only for failures clearly caused by the `archlinux:latest` image or its mirrors.
- `tools/version.py` derives the version from git at configure time. Dev and CI builds keep upstream's `YYYY.WW.0~N.hash8` string; packagers that build outside a git checkout write a `.version` file instead.

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
