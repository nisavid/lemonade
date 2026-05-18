---
name: working-with-upstream-refs
description: "Use when work in this Lemonade fork needs upstream commits, upstream release tags, upstream/main, upstream-stable, fork sync baselines, upstream version baselines, comparisons between nisavid/lemonade and lemonade-sdk/lemonade, or repo-specific inputs for syncing-forks-with-upstream."
---

# Working With Upstream Refs

## Overview

Use before upstream Git-ref work. This fork separates live upstream development from the stable release baseline.

## Ref Roles

| Ref | Owner | Role |
| --- | --- | --- |
| `upstream/main` | Git remote-tracking ref | Upstream `main` after `git fetch upstream --prune --tags`. |
| `upstream-main` | Local branch, mirrored to `origin/upstream-main` | Scouting branch for upstream's latest commits; tracks `upstream/main`. |
| `origin/upstream-stable` | Fork remote-tracking ref | Published stable upstream baseline for sync and fork release versioning. |
| `upstream-stable` | Local branch | Maintenance branch for advancing the stable baseline; tracks `origin/upstream-stable`. |

`upstream` fetches from `https://github.com/lemonade-sdk/lemonade.git` and has push URL `DISABLED`. Publish fork-local refs to `origin`, not upstream.

## When to Use Which Ref

- Use `upstream-main` or `upstream/main` for unreleased changes, recent-commit investigations, and future sync estimates.
- Use `origin/upstream-stable` for sync and fork release baselines.
- Use local `upstream-stable` only when maintaining that published baseline.
- Choose the next stable baseline from live GitHub Releases, not tag sorting. Ignore drafts and prereleases unless explicitly requested.

## Scout Current Upstream State

```bash
git fetch origin
git fetch upstream --prune --tags
git log --oneline --left-right --cherry-pick origin/main...upstream/main
git diff --stat origin/main..origin/upstream-stable
```

## Update Published Upstream Refs

Fetch-only scouting does not need a push. Update and push `upstream-main` when a task depends on a shared current view of upstream `main`, such as an upstream commit investigation, proactive sync estimate, handoff, issue, or PR description.

```bash
git fetch origin refs/heads/upstream-main:refs/remotes/origin/upstream-main
git fetch upstream --prune --tags
git switch upstream-main
git branch --set-upstream-to=upstream/main upstream-main
git pull --ff-only
git push origin upstream-main
git fetch origin refs/heads/upstream-main:refs/remotes/origin/upstream-main
git rev-parse upstream/main upstream-main origin/upstream-main
```

Advance and push `upstream-stable` only when choosing a new stable upstream release baseline for fork sync or fork versioning, such as refreshing the sync baseline, preparing a sync to latest stable, or creating a fork version from a newer upstream release. Do not advance it just because a new tag exists; use GitHub Releases and operator intent.

```bash
gh release list --repo lemonade-sdk/lemonade --exclude-drafts --exclude-pre-releases --limit 1 --json tagName,publishedAt,isDraft,isPrerelease
git fetch origin refs/heads/upstream-stable:refs/remotes/origin/upstream-stable
git fetch upstream --prune --tags
git switch upstream-stable
git branch --set-upstream-to=origin/upstream-stable upstream-stable
git pull --ff-only
git merge --ff-only <release-tag>
git push origin upstream-stable
git fetch origin refs/heads/upstream-stable:refs/remotes/origin/upstream-stable
git rev-parse <release-tag> upstream-stable origin/upstream-stable
```

If `<release-tag>` is not a fast-forward, stop and ask whether to move the baseline. Do not force-push this branch as routine maintenance.

## Input to Syncing Forks With Upstream

When using `syncing-forks-with-upstream` in this repo, use `origin/upstream-stable` as the default upstream baseline. Use `upstream/main` only when the user explicitly asks to sync unreleased upstream `main`.

## Sync the Fork From Upstream Stable

Start from the fork's current mainline, then merge the stable baseline so upstream commit identity stays intact.

```bash
git fetch origin
git fetch upstream --prune --tags
git switch -c nisavid/sync-upstream-stable origin/main
git merge origin/upstream-stable
git merge-base --is-ancestor origin/upstream-stable HEAD
```

Resolve conflicts, validate, and publish to `origin`. Do not rebase upstream commits, replay them as patches, or use history-replacing sync flows unless the user explicitly asks.

## Common Mistakes

- Treating `upstream-main` as the default sync source. It is for unreleased scouting unless the user requests upstream `main`.
- Treating `upstream-stable` as upstream-maintained. It is fork-owned.
- Updating the stable baseline from the newest tag without checking GitHub Release state.
- Leaving a shared investigation or baseline update local-only after the task depends on other agents seeing it.
- Opening upstream PRs or issues without explicit user direction.
- Accepting patch-equivalent content when upstream ancestry is not preserved.

## Closeout Checks

- `git remote -v` shows the upstream fetch URL and push URL `DISABLED`.
- `git branch -vv --list upstream-main upstream-stable` shows the intended tracking relationships.
- Ref update work proves the intended refs match with `git rev-parse`.
- Sync branches pass `git merge-base --is-ancestor origin/upstream-stable HEAD` after `git fetch origin` when they claim to include the stable upstream baseline.
