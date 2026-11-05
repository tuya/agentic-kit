# Manual contribution sync runbook: Mode A

This runbook targets GitLab 17.8.7 with CE/Free capabilities. GitHub accepts fork PRs;
GitLab accepts direct MRs. All official merges happen in GitLab, with official branches
and tags synced one way to GitHub. Maintainers perform these steps manually in this phase;
no App, Webhook, or automated sync script is deployed.
See [CONTRIBUTING.md](../../CONTRIBUTING.md) for general commit and documentation rules.

## 1. Prerequisites

- Only the sync account may update official branches in the GitHub upstream repository.
  Separate rules block force pushes and deletion. Use this account only to publish results
  already merged in GitLab; never use GitHub's merge button or merge API.
- Protect the GitLab upstream repository's default and release branches against direct pushes,
  force pushes, and deletion. Maintainers merge through MRs. Select
  `Merge commit with semi-linear history`, set squashing to `Do not allow`, and enable
  `Pipelines must succeed`.
- Create a dedicated, controlled GitLab fork in a namespace without production credentials,
  inherited internal runners, or sensitive variables. Initially disable its CI/CD and prevent
  pipelines from this fork from running in the upstream project. An administrator must disable
  `ci_allow_fork_pipelines_to_run_in_parent_project` and verify the restriction.
- Until isolated CI passes acceptance checks, push GitHub PR code only to the controlled fork,
  never to the upstream repository where internal CI would run automatically.
  `ci.skip`, commit messages, and manual conventions are not execution isolation boundaries.
- Use the maintainer's sync checkout only for Git/API operations. Run contribution code,
  builds, and submodule updates in an isolated environment.
- Confirm that `origin` is the GitLab upstream repository and `github` is `tuya/agentic-kit`.
  Configure an additional remote once:

  ```bash
  git remote -v
  git remote add intake '<actual Git URL of the controlled GitLab fork>'
  ```

  Replace the placeholder before running it. If `intake` already exists, check its URL instead
  of adding it again. The examples use `master`; replace it throughout if the actual default
  branch has another name. Stop on any failure, investigate, and only then continue.

## 2. Create a contribution record

- Track each GitHub PR by its original full URL, repository ID, PR number, author, and owner.
- Keep at most one active canonical MR per PR. Check existing records before creating or updating it.
- The canonical MR targets the GitLab upstream repository from the controlled fork's
  `contrib/github/pr-N` branch. It holds final validation, review, and merge records.
  Keep the GitHub PR open as the author's communication and update channel until completion.
- Record at least the source head `H`, target commit `B`, final merge commit `M`,
  GitHub's official branch tip after sync `S`, validation pipeline, reviews, sync time, and operator.
  Leave unavailable fields incomplete; never prefill successful results.
- Cross-link the PR and MR descriptions with full URLs and state that GitLab owns the merge.
  Do not treat `#N` as `!N`. The fork's mirrored branch belongs to this contribution;
  do not manually append commits that differ from the GitHub source.

## 3. Read and import a GitHub PR

1. Use the GitHub page or API to verify the repository, target branch, PR state, and full `head.sha`.
   Read the contributor's head, not `merge_commit_sha` or `refs/pull/N/merge`.
2. Replace the PR number and expected SHA below; fetch the PR ref from the fixed `github` remote:

   ```bash
   PR=123
   EXPECTED_H='<full head SHA of the current GitHub PR>'
   git fetch --no-tags github "refs/pull/${PR}/head"
   H=$(git rev-parse FETCH_HEAD)
   test "$H" = "$EXPECTED_H"
   git update-ref "refs/contrib/github/pr-${PR}/${H}" "$H"
   ```

   If a comparison fails, read the current PR again. Do not use arbitrary clone URLs from pages
   or comments. Retain the source SHA, fetch time, and local evidence ref so a later force push
   does not make an old revision untraceable.
3. Fetch the current GitLab target and confirm it is an ancestor of the source commit to be tested:

   ```bash
   git fetch --no-tags origin master
   B=$(git rev-parse FETCH_HEAD)
   git merge-base --is-ancestor "$B" "$H"
   ```

   If this fails, first sync the GitLab mainline to GitHub, then ask the author to merge the latest
   mainline into their GitHub branch. Fetch the new `H`; do not rebase, cherry-pick, or squash during import.
4. Push the original commits unchanged to the controlled fork, then create or update the canonical MR:

   ```bash
   git push intake "${H}:refs/heads/contrib/github/pr-${PR}"
   ```

5. Record `H`, `B`, and the source PR in the MR. Keep it Draft initially and verify its source branch
   SHA equals `H`. Direct GitLab contributors open regular MRs under the same target `B`, tested `H`,
   review, and centralized merge requirements.

## 4. Updates and narrowly scoped replacement

- When GitHub receives new commits, read the latest state and new `H`, then repeat the checks
  and normal push above. Update the same canonical MR's source revision, validation evidence,
  and discussion; retain evidence for earlier revisions.
- New commits invalidate prior validation and approval. Return the MR to Draft and validate
  and review the new `H` again.
- If the author force-pushes and a normal push is rejected, first confirm the MR is unmerged
  and the branch belongs only to this PR. Preserve the old `H` as a Git ref along with validation
  and review records, then read the controlled fork branch's current SHA as `OLD`.
  Only after a maintainer checks these facts may this single unmerged contribution branch be replaced:

  ```bash
  OLD='<just-verified full old SHA of the controlled fork branch>'
  git push --force-with-lease="refs/heads/contrib/github/pr-${PR}:${OLD}" \
    intake "${H}:refs/heads/contrib/github/pr-${PR}"
  ```

  A failed lease means the branch changed; stop and check again. Never broaden this to an
  unconditional force push. This exception never applies to official upstream branches or tags.

## 5. Isolated validation, review, and merge

- Before isolated CI passes acceptance checks, a separate temporary VM may run preliminary checks.
  Attach its environment, `H`, `B`, commands, actual test counts, exit results, and log locations
  to the MR. The VM must have neither production credentials nor internal network access.
- VM reports are preliminary or supplementary evidence. They do not replace the qualifying pipeline
  required by `Pipelines must succeed`. Without that pipeline, keep the MR Draft and do not merge;
  manual sync does not waive CI.
- After the isolated runner, network, credentials, and CI configuration pass acceptance checks,
  a maintainer enables fixed, reviewed CI in the controlled fork. CI configuration changes in the
  contribution branch must not change the trusted entry point used for execution. Confirm that
  the canonical MR recognizes a successful fork pipeline for `H`; verify this on the actual instance
  before allowing normal merges.
- At least one person other than the author reviews the current `H`. On Free, the maintainer
  manually checks review records and outstanding discussions. Preserve the authors and links of
  GitHub reviews; do not use a bot or another person's account to impersonate a GitLab approval.
- Immediately before merging, read the current PR head, MR source head, GitLab target tip, and
  pipeline again. Both sources must equal the tested and reviewed `H`; current `B` must be an
  ancestor of `H`; all required jobs must pass. If any SHA changes, reassess and update the source,
  rerun validation, and repeat review as needed.
- Create `M` with a semi-linear merge that preserves the original contribution commits;
  do not rebase or squash at merge time. Record `M` and verify that both `H` and that merge's `B`
  are reachable from `M` before syncing outward.

## 6. Publish the GitLab mainline to GitHub

1. Fetch both mainlines. Allow a normal fast-forward push only if GitHub's tip is an ancestor of GitLab's:

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

2. Compare the pushed `GL` with GitHub's actual remote SHA. If the source advanced in the meantime,
   record the range actually synced and perform the remaining fast-forward update afterward.
   Confirm that this contribution's `M` is in the published mainline and record `S`.
3. Sync only explicitly registered release branch refs, checking ancestry separately for each.
   Do not use `git push --mirror`, wildcard branch pushes, `--force`, or remote ref deletion.
4. Push only the specific official version tag being published. Compare each platform's tag object
   SHA and `^{}` peeled commit SHA. These have different meanings for annotated tags; both must match
   across platforms. First confirm that the tag points to the approved release commit.

   ```bash
   git fetch origin tag vX.Y.Z
   git ls-remote --tags origin 'refs/tags/vX.Y.Z' 'refs/tags/vX.Y.Z^{}'
   git ls-remote --tags github 'refs/tags/vX.Y.Z' 'refs/tags/vX.Y.Z^{}'
   git push github refs/tags/vX.Y.Z:refs/tags/vX.Y.Z
   git ls-remote --tags github 'refs/tags/vX.Y.Z' 'refs/tags/vX.Y.Z^{}'
   ```

   Replace `vX.Y.Z` with the actual version. If GitHub has a tag of that name with different values,
   stop, preserve the discrepancy, and escalate.

## 7. Close the GitHub PR loop

- Follow this order: merge in GitLab, sync and verify the GitHub mainline, then update the PR result.
  If mainline sync fails, record `Merged; awaiting sync` and keep the tracking task open.
- Before closing the PR, read its actual state and current `head.sha` again. Check whether that head
  is included in the synced mainline `S`. If a new head was not accepted, record its SHA and follow-up
  PR/MR, and explain publicly that only this revision `H` was accepted. Settle the treatment of those
  new changes before closing; never describe an old revision's result as acceptance of the new head.
  GitHub may recognize an indirect merge when the original `H` is reachable from the base.
  Use GitHub's actual result; a merged GitLab MR does not by itself mean the GitHub PR is marked merged.
- If GitHub does not mark it merged automatically, comment with the actual GitLab MR, `H`, `M`, and
  sync result, then close the PR. Never merge again on GitHub just to display merged status.
  Preserve the original author identities and commit attribution.
- Private GitLab links may support internal audits; also provide a useful public summary and GitHub
  commit links. Do not copy private logs, credentials, internal discussions, or restricted resources
  into public comments. Failure summaries must still explain how the author can proceed.

## 8. Exceptions and acceptance checks

- If a PR closes before merge, pause sync and validation, close its unmerged MR, and retain the records.
  If it reopens, check the current state and latest `H`, and prefer reopening the original MR.
  A reopened PR must not cause an already merged contribution to be merged again.
- If commits arrive during closure, reopening, merge, or sync, check the current API state.
  Treat a new `H` not included in `M` as unaccepted work and use a follow-up PR/MR;
  do not apply an old revision's success to the new revision.
- On missing permissions, protection-rule rejection, or failed tag checks, stop that step and ask a
  maintainer to fix the configuration before retrying. Do not temporarily relax official branch
  protections or use the sync account to merge on GitHub.
- On network or sync failure, retain `H/B/M/S` and operation results, fetch again, and resume from the
  unfinished step. Check actual remote state first: a lost response may have followed a successful
  operation and must not result in a second, different change.
- On mainline divergence, preserve both tips, freeze sync, and open an incident MR. Include GitHub's
  original unique commits in an isolated recovery branch and merge in the latest GitLab mainline,
  making target `B` an ancestor of the recovery head. A later revert may remove unwanted content
  while retaining the original GitHub tip in history; cherry-picking content alone cannot restore
  ancestry. After isolated validation and non-author review, merge the incident MR only in GitLab,
  then confirm that sync can fast-forward. Do not overwrite or delete GitHub-only commits.
  If that history cannot be preserved safely, keep sync frozen and agree on a separate recovery plan.
- Before rollout, use rehearsal PRs to test normal import, repeated updates, force-with-lease conflicts,
  target advances, stale CI results, PR closure/reopening, sync failure and recovery after GitLab merge,
  same-name tags with different values, blocked human merges and history rewrites on GitHub,
  private-log isolation, and both successful and failed isolated fork CI. Record actual results and
  owners. Leave failed gate checks unresolved; do not claim the workflow is live before acceptance.
