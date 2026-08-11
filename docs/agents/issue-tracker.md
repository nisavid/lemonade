# Issue tracker: GitHub

Issues and PRDs for this repo live in GitHub Issues for `nisavid/lemonade`. Use the `gh` CLI for issue operations.

This is the fork's issue tracker. Create or update upstream `lemonade-sdk/lemonade` issues only when the user explicitly says upstream issue work is in scope.

## Conventions

- **Create an issue**: `gh api repos/nisavid/lemonade/issues -X POST -f title="..." -f body="..."`
- **Read an issue**: `gh issue view <number> --repo nisavid/lemonade --comments`
- **List issues**: `gh issue list --repo nisavid/lemonade --state open --json number,title,body,labels,comments`
- **Comment on an issue**: `gh issue comment <number> --repo nisavid/lemonade --body "..."`
- **Apply a label**: `gh issue edit <number> --repo nisavid/lemonade --add-label "..."`
- **Remove a label**: `gh issue edit <number> --repo nisavid/lemonade --remove-label "..."`
- **Close an issue**: `gh issue close <number> --repo nisavid/lemonade --comment "..."`

When a skill needs filtered issue data, prefer `--json` plus `--jq` over parsing human-formatted output.

## Wayfinding operations

A Wayfinder launcher is an ordinary issue labelled `question`. It becomes a map only when a charting session defines the destination and replaces the launcher with, or links it to, a canonical map labelled `wayfinder:map`.

Use these labels for map artifacts:

| Artifact | Label |
| --- | --- |
| Canonical map | `wayfinder:map` |
| AFK research ticket | `wayfinder:research` |
| HITL prototype ticket | `wayfinder:prototype` |
| HITL grilling ticket | `wayfinder:grilling` |
| Decision-unblocking task | `wayfinder:task` |

Create the map and every currently specifiable ticket first. Wire relationships only after every issue has a GitHub database id.

### Parent and child issues

Use GitHub's native sub-issue relationship. Fetch the child issue's database id, then attach it to the map:

```bash
child_id="$(gh api repos/nisavid/lemonade/issues/<ticket-number> --jq .id)"
gh api repos/nisavid/lemonade/issues/<map-number>/sub_issues \
  -X POST \
  -F sub_issue_id="$child_id"
```

List a map's children with:

```bash
gh api --paginate repos/nisavid/lemonade/issues/<map-number>/sub_issues
```

### Blocking relationships

Use GitHub's native issue-dependency relationship. The endpoint names the blocked issue; `issue_id` names the blocker:

```bash
blocker_id="$(gh api repos/nisavid/lemonade/issues/<blocker-number> --jq .id)"
gh api repos/nisavid/lemonade/issues/<blocked-number>/dependencies/blocked_by \
  -X POST \
  -F issue_id="$blocker_id"
```

List an issue's blockers with:

```bash
gh api --paginate repos/nisavid/lemonade/issues/<issue-number>/dependencies/blocked_by
```

An open child is on the frontier only when it is unassigned and its `blocked_by` response is empty. Claim a frontier ticket before working it:

```bash
gh issue edit <ticket-number> --repo nisavid/lemonade --add-assignee @me
```

Resolve one non-research ticket per session. Post the answer as a resolution comment, close the ticket, and append one linked gist to the map's **Decisions so far** section. Refer to maps and tickets by linked title in human-facing prose; numbers are only operational identifiers.

GitHub documents the underlying [sub-issue](https://docs.github.com/en/rest/issues/sub-issues) and [issue-dependency](https://docs.github.com/en/rest/issues/issue-dependencies) APIs.

## When a skill says "publish to the issue tracker"

Create a GitHub issue in `nisavid/lemonade`.

## When a skill says "fetch the relevant ticket"

Run `gh issue view <number> --repo nisavid/lemonade --comments`.

## Pull request review automation

When a review bot excludes bot-authored pull requests, first test whether the
exclusion is tied to the pull request author or only to the bot-authored commits:
push the agent-authored correction to the existing pull request branch, refresh
the review state, and check whether the bot reviews the updated diff. If the
pull request remains excluded after agent-authored changes land, create a
replacement fork-local branch and pull request with the same intended dependency
update, then close the excluded bot-authored pull request after the replacement
merges.
