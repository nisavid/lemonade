# Issue tracker: GitHub

Issues and PRDs for this repo live in GitHub Issues for `nisavid/lemonade`. Use the `gh` CLI for issue operations.

This is the fork's issue tracker. Create or update upstream `lemonade-sdk/lemonade` issues only when the user explicitly says upstream issue work is in scope.

## Conventions

- **Create an issue**: `gh issue create --repo nisavid/lemonade --title "..." --body "..."`
- **Read an issue**: `gh issue view <number> --repo nisavid/lemonade --comments`
- **List issues**: `gh issue list --repo nisavid/lemonade --state open --json number,title,body,labels,comments`
- **Comment on an issue**: `gh issue comment <number> --repo nisavid/lemonade --body "..."`
- **Apply a label**: `gh issue edit <number> --repo nisavid/lemonade --add-label "..."`
- **Remove a label**: `gh issue edit <number> --repo nisavid/lemonade --remove-label "..."`
- **Close an issue**: `gh issue close <number> --repo nisavid/lemonade --comment "..."`

When a skill needs filtered issue data, prefer `--json` plus `--jq` over parsing human-formatted output.

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
