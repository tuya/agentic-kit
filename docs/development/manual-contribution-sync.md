# Manual contribution sync runbook

GitHub PRs and GitLab MRs are both entry points; each contribution has one canonical
GitLab MR (tracked by the PR URL), all merges happen in GitLab, and official branches
and tags sync one way to GitHub. Manual phase for GitLab 17.8.7 CE/Free: no automation
is deployed; any future automation needs review before it is enabled and never merges
on its own. See [CONTRIBUTING.md](../../CONTRIBUTING.md) for policy.

Notation: `H` source head, `B` target commit, `M` merge commit, `S` synced GitHub tip.
MR states: `Awaiting import`, `In review`, `Awaiting author update`, `Ready to merge`,
`Merged; awaiting sync`, `Synced; awaiting closure`, `Complete`, `Rejected/withdrawn`.
Record `H/B/M/S`, pipeline, reviews, sync time and operator in the MR; never prefill.
Historical merged/closed PRs need no replacement MRs -- archive URL and discussion.

## 1. One-time setup

- GitHub: rulesets restrict branch creation and updates; a separate no-bypass ruleset
  blocks force pushes and deletion; the `Always allow` sync bypass goes only to the
  sync account (it can technically also merge PRs -- audit it); only sync maintainers
  create tags, and published tags are never updated or deleted; auto-merge, merge
  queues and merge bots stay disabled on both platforms; fork PRs run on minimal
  read-only runners, never `pull_request_target`. Protection-rule changes are
  emergency changes: record the reason, operator and restoration result.
- GitLab: default and release branches protected against direct push, force push and
  deletion; `Merge commit with semi-linear history`, squashing `Do not allow`,
  `Pipelines must succeed`.
- Controlled fork: no production credentials, inherited runners or sensitive
  variables; only designated maintainers push to it; CI/CD stays disabled until
  isolation is verified (administrator disables
  `ci_allow_fork_pipelines_to_run_in_parent_project` and verifies). Until then PR
  code goes only to this fork, never the upstream where internal CI would run
  automatically -- `ci.skip` is not an isolation boundary. Run contribution code and
  builds in an isolated environment, not the sync checkout.

```bash
git remote -v
git remote add intake '<actual Git URL of the controlled GitLab fork>'
```

`origin` = GitLab upstream, `github` = tuya/agentic-kit. Examples use `master`.
Stop on any failure.

## 2. Import, update, and merge a GitHub PR

```bash
PR=123
EXPECTED_H='<full head SHA of the current GitHub PR>'
git fetch --no-tags github "refs/pull/${PR}/head"
H=$(git rev-parse FETCH_HEAD)
test "$H" = "$EXPECTED_H"
git fetch --no-tags origin master
B=$(git rev-parse FETCH_HEAD)
git merge-base --is-ancestor "$B" "$H"
git push intake "${H}:refs/heads/contrib/github/pr-${PR}"
```

- Import only the author's head (`refs/pull/N/head`, never GitHub's test merge ref);
  never rebase, cherry-pick, squash, or append commits of your own. Ancestry check
  failed: publish the mainline first (section 3), ask the author to merge it, then
  re-import the new head.
- Before intake, inspect CI configuration, build scripts, dependencies and submodule
  changes; contribution code must never change the CI entry point that executes it.
- New `H` invalidates prior validation and approval: re-import, re-validate,
  re-review. Author force-pushed: replace only the unmerged intake branch with
  `git push --force-with-lease`, never official branches or tags.
- Merge in GitLab only: green pipeline for the exact tested and reviewed `H`,
  non-author review, semi-linear merge preserving the original commits. Record `M`;
  verify `H` and `B` are reachable from `M` before syncing outward.

## 3. Publish the GitLab mainline and tags to GitHub

```bash
git fetch --no-tags origin master
GL=$(git rev-parse FETCH_HEAD)
git fetch --no-tags github master
GH=$(git rev-parse FETCH_HEAD)
git merge-base --is-ancestor "$GH" "$GL"
git push github "${GL}:refs/heads/master"
git ls-remote --heads origin refs/heads/master
git ls-remote --heads github refs/heads/master
```

```bash
git fetch origin tag vX.Y.Z
git ls-remote --tags origin 'refs/tags/vX.Y.Z' 'refs/tags/vX.Y.Z^{}'
git ls-remote --tags github 'refs/tags/vX.Y.Z' 'refs/tags/vX.Y.Z^{}'
git push github refs/tags/vX.Y.Z:refs/tags/vX.Y.Z
git ls-remote --tags github 'refs/tags/vX.Y.Z' 'refs/tags/vX.Y.Z^{}'
```

- Mainline: fast-forward only -- no `--mirror`, wildcard pushes, `--force` or ref
  deletion. Registered release branches sync the same way, ancestry checked per
  branch. Compare the pushed `GL` with the actual remote SHA afterward, record the
  synced range and `S`, and confirm this contribution's `M` is in the mainline.
- Tags: the tag object SHA and the peeled `^{}` commit SHA must both match across
  platforms, and the tag must point to the approved release commit. A same-name tag
  with different values: stop and escalate. A published GitHub Release mirrors the
  same tag and commit and links the GitLab release record.

## 4. Close the GitHub PR

- Order: merge in GitLab -- sync and verify -- update the PR. Sync failed: record
  `Merged; awaiting sync` and keep the tracking task open.
- Re-read the PR state and `head.sha` before closing. GitHub may mark an indirect
  merge when `H` is reachable from the base; use its actual result. If not marked
  merged: comment the GitLab MR, `H`, `M` and sync result, then close -- never
  re-merge on GitHub just to show merged. A newer unaccepted head is a follow-up
  PR/MR. Public comments stay useful without private logs, credentials or restricted
  discussions.

## 5. Exceptions

- PR closed or reopened: pause sync, close (or reopen) the unmerged MR, keep the
  records; a reopened PR never re-merges an already merged contribution; a new `H`
  not included in `M` is unaccepted work -- follow-up PR/MR.
- Permission, protection-rule or tag-check failure: stop, fix the configuration,
  retry; never relax protections or merge on GitHub.
- Network or sync failure: keep `H/B/M/S` and results, re-fetch, resume from the
  unfinished step; check the actual remote state first (a lost response may have
  followed a successful operation).
- Mainline divergence: preserve both tips, freeze intake, releases and sync, open an
  incident MR whose recovery branch keeps GitHub's commits in history with `B`
  ancestral; validate and review it, merge in GitLab, then fast-forward. Never
  overwrite or delete GitHub-only commits.
- Before rollout: record the actual URLs, tips, fork, owners, protection rules and
  isolation status; verify the protections actually hold -- regular members cannot
  push or merge on GitHub, the sync bypass cannot force-push, maintainers can merge
  in GitLab and fast-forward GitHub (on Free, unapproved MRs are rejected manually);
  rehearse with rehearsal PRs (normal import, repeated updates, force-push
  replacement, target advance, stale CI, closure/reopening, sync failure and
  recovery, tag conflicts, blocked merges and history rewrites, successful and
  failed fork CI). Do not declare the workflow live with unresolved gate checks.
