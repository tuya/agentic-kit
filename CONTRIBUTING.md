# Development guidelines: Mode A (manual phase)

Both GitHub PRs and GitLab MRs are contribution entry points. Each contribution has one
canonical GitLab MR for final review, validation and merging. All official merges, version
tags and primary releases originate in GitLab. Maintainers manually sync the merged results
to GitHub, which remains the public PR discussion channel and code mirror.

These guidelines target the **CE/Free capabilities of GitLab 17.8.7**. They do not require
paid approval features, merge trains, bots or webhooks. Follow the workflow once these
guidelines are merged. Repository protection, CI isolation and account permissions require
separate verification; this document does not establish that those settings are already in
place. Commands are documented in the [manual sync runbook](docs/development/manual-contribution-sync.md).

## 1. Platform responsibilities and repository protection

### Contribution entry points and roles

- Read [AGENTS.md](AGENTS.md) before making changes. Use [CONTEXT-MAP.md](CONTEXT-MAP.md)
  to find the relevant module's domain documentation.
- External contributors develop in their GitHub forks and open PRs. Members with GitLab
  access can open MRs directly. Update an existing canonical MR for the same change;
  a contribution originating in GitLab does not need a separate GitHub PR.
- `origin` points to the primary GitLab project; `github` points to `tuya/agentic-kit`.
  Maintainers importing external PRs also configure `intake` for a controlled fork of the
  GitLab project. Verify the actual URLs with `git remote -v` before operating.
- Authors implement changes and complete their records. Module reviewers perform code
  review. Merge maintainers perform final acceptance checks. Sync maintainers import
  contributions, sync official branches and close the GitHub feedback loop. Assign merge
  and sync owners to each MR; one maintainer may perform both roles.
- Regular developers use GitLab Developer and GitHub Read/Triage permissions. Limit
  Maintainer access and GitHub sync bypass permissions to designated maintainers, and
  check permissions inherited from groups.

### Required GitLab settings

- Protect the default branch (`master` in these examples) and any active `release/*`
  branches. Set `Allowed to push and merge = No one` and `Allowed to merge = Maintainers`;
  disable force pushes. Review overlapping rules: GitLab 17.8.7 applies the more permissive
  push/merge permissions among matching rules.
- Protect `v*` tags. Allow creation only by the Maintainer role assigned to release
  maintainers. Published tags must remain unchanged.
- Select `Merge commit with semi-linear history` and set squashing to `Do not allow`.
  The validated source branch must contain the current target branch tip. When an update
  is needed, the author merges the latest target branch into their source branch and
  repeats validation. Do not rebase or squash imported GitHub commits in GitLab; preserve
  the original PR commit SHAs.
- Enable `Pipelines must succeed` and `All threads must be resolved`. Maintainers must
  also check the actual jobs and commit SHA. Keep unattended auto-merge disabled.
  Assign one maintainer at a time to process merges into each target branch.
- Approval by at least one reviewer other than the author is a team policy. CE/Free
  approvals do not enforce a merge block, and a CODEOWNERS file does not enforce required
  approval. The merge maintainer must perform the checks in these guidelines manually.

### Required GitHub settings

- Branches in the primary GitHub repository receive only content already merged in GitLab.
  Contribution branches stay in contributors' forks. Do not mirror GitLab `contrib/*` or
  internal development branches into the primary GitHub repository.
- Use active rulesets to restrict branch creation and updates; strict coverage includes
  every branch in the primary repository. Grant an `Always allow` bypass for direct sync
  only to the designated sync team/account. Regular members receive no bypass.
- Use a separate ruleset with no bypass to block force pushes and deletion of official
  branches. Only sync maintainers may create release tags; existing tags must not be
  updated or deleted. Verify rule availability against the actual GitHub plan.
- Nobody, including sync maintainers, may merge a PR through GitHub's Merge, Squash or
  Rebase merge controls, API or CLI. Disable GitHub auto-merge, merge queues and other
  automated merge bots.
- **An account with sync bypass permissions can technically also merge PRs.** GitHub cannot
  distinguish permission to fast-forward a branch from permission to merge into that same
  branch. Limit access, require per-batch sync records and audit operations. Changes to
  protection rules are emergency changes; record the reason, operator and restoration result.

## 2. Historical PRs and ongoing contributions

- Preserve the platform, number, full URL and discussion of historical merged or closed
  GitHub PRs. They do not need replacement GitLab MRs. Maintainers archive descriptions,
  authors, timestamps, states, comments, reviews, inline discussions, merge SHAs and necessary
  attachments, and record the archive location.
- Handle existing open and new GitHub PRs through Mode A: manually import the exact head SHA
  and create the associated GitLab MR. Keep the GitHub PR open for author updates and public
  discussion. The final merge takes place in GitLab.
- Each GitHub PR has at most one active canonical GitLab MR. Search by the original PR URL
  before creating one. When a PR is reopened, prefer reopening its original unmerged MR.
  Use a new PR/MR for new work following a completed contribution.
- Treat a full historical import as a separate migration task. Test it in a temporary
  project, verify the results, and preserve old-to-new URL and commit SHA mappings.
  Git pushes transfer code; they do not archive comments or review metadata.
- Record important design decisions in the module's `CONTEXT.md` or `docs/adr/`, with source
  links. Keep sync operation records in the canonical GitLab MR. Keep contributor-facing
  summaries and publicly shareable evidence in the GitHub PR.

## 3. References, associations and sync records

- Use the platform, record type, number and full URL in documentation, CHANGELOG entries,
  commit messages and cross-platform discussions. Use Markdown links where supported;
  include the full URL in plain-text commit messages.
- GitHub example: `[GitHub PR #47](https://github.com/tuya/agentic-kit/pull/47)`.
- GitLab example: `[GitLab MR !12](https://gitlab.example.com/group/project/-/merge_requests/12)`.
  The GitLab host, project and number are examples. Replace them with the actual page URL.
- In GitLab, `#N` identifies an issue and `!N` identifies an MR. Numbers are independent
  across platforms; never mechanically replace a historical `#N` with `!N`. Existing bare
  `(#N)` references in CHANGELOG identify GitHub PRs in `tuya/agentic-kit`.
- Link each GitHub PR and its canonical GitLab MR in both directions. For contributions
  originating in GitLab, write `N/A` in the source PR field. If external contributors cannot
  access an internal GitLab URL, also provide a public progress summary, actionable failure
  details and official commit links in GitHub. Do not copy credentials, private logs or
  restricted discussions into public summaries.
- Maintain these fields in the MR template: source PR URL/number, intake fork/branch,
  source head SHA `H`, target SHA before merging `B`, review and pipeline SHAs, final merge
  SHA `M`, official branch tip synced in the batch `S`, sync time, operator, state and
  incident records. During this phase, this GitLab MR record is the authoritative manual
  association and progress record.
- Use these states consistently: `Awaiting import`, `In review`, `Awaiting author update`,
  `Ready to merge`, `Merged; awaiting sync`, `Synced; awaiting closure`, `Complete`, and
  `Rejected/withdrawn`. Record incident details separately. A merged change is not necessarily
  a synced change.
- When `H` changes, retain the previous SHA and operation history, and recheck review and
  test evidence for the new revision. A development branch may temporarily use `MR pending`
  before the MR exists. Resolve this change's pending references before merging, and all
  pending references in a release's scope before publishing it.

## 4. Branches and manual intake

### Contributions originating in GitLab

- Create `feature/<topic>`, `fix/<topic>`, `docs/<topic>` or `chore/<topic>` from the GitLab
  default branch. Push to `origin` and open an MR. Target maintenance fixes at the branch
  designated by the maintainer.
- Give each branch one independently reviewable objective. Specify the problem, owner,
  acceptance criteria and related issues. Small changes may record these directly in the
  MR without a separate issue.

### Manual intake of GitHub PRs

1. The sync maintainer verifies the target branch, author, scope and current GitHub head
   SHA. Inspect CI configuration, CMake/scripts, dependencies and submodule changes before
   accepting the contribution for intake.
2. Fetch `refs/pull/<N>/head` from the fixed upstream repository, `tuya/agentic-kit`, and
   verify that its SHA matches the current PR head. Import the original head, not GitHub's
   test merge ref.
3. Push the original commit to `contrib/github/pr-<N>` in the controlled GitLab fork, and
   create or update the canonical MR targeting the primary project. Only designated
   maintainers may write to this fork. Disable its CI/CD until isolated CI is verified,
   and keep the MR in Draft. Do not push external commits into primary-project branches
   that automatically execute internal CI.
4. Complete isolated validation, independent review and the required records before marking
   the MR ready to merge. Recheck that the imported head still matches the current GitHub
   head before merging.
5. Authors continue pushing updates to the original GitHub fork. Maintainers import each
   update and record the new head. When maintainers need documentation or code changes,
   prefer having the author incorporate them into the original branch. Do not maintain
   divergent implementations on the two platforms.

### Commit requirements

- **Every PR/MR title and every new commit message must be in English.** This includes
  commit subjects, bodies and merge commit messages, even for changes limited to
  `docs-site/` or the Chinese `README.md`. File-content language exceptions do not apply
  to contribution titles or commit messages.
- Preserve published history. If an unmerged source commit message needs correction,
  its author updates it on the original source branch; maintainers then import and validate
  the new head SHA. Importers must not rewrite source commits themselves.
- Use Conventional Commits, for example `feat(iot-client): ...`, `fix(tuya-ble): ...`,
  `docs: ...` or `chore: ...`. Mark breaking changes with `!` and explain the migration.
- Explain the reason for the change and include necessary full source links. End the body
  with actual validation results and counts; explain checks that were not run.
  Agent-authored commits include a `Co-Authored-By:` trailer. Preserve the original author,
  content and SHA when importing a contributor's commits.
- See the [manual sync runbook](docs/development/manual-contribution-sync.md) for commands
  and the procedure for a non-fast-forward intake update.

## 5. Implementation and CI validation

### Language and code comments

- **English is the required authoring language, with two file-content exceptions:**
  `docs-site/` supports multilingual content, and the root `README.md` uses Chinese prose.
  The root `README_en.md` uses English. Module documentation and example documentation
  remain English. The Chinese root `README.md` may also use Chinese explanatory comments
  in documentation examples and directory listings. Keep commands and API identifiers accurate.
- Use English for code identifiers and names, including functions, types, variables,
  macros, enum values, files and directories. Follow the domain terms in the relevant
  `CONTEXT.md`.
- Outside `docs-site/`, write comments in actual source, test, build and script files in English:
  inline and block comments, file headers, Doxygen/API comments, docstrings, TODOs, FIXMEs
  and comments in example code.
  Explain intent, constraints, ownership and non-obvious behavior clearly.
- Subject to the file-content exceptions above, write repository documentation,
  `CONTEXT.md`, ADRs, CHANGELOG entries, templates, scripts, build/CI configuration comments,
  diagram labels and usage instructions in English.
  Logs, errors, diagnostics, test names and assertion explanations must also be English.
- Use English for commit subjects and bodies, PR/MR titles and descriptions, review
  comments, and maintainer-authored sync summaries and records. These contribution records
  must be English even when the changes only affect `docs-site/` or the Chinese `README.md`.
- This is an authoring-language rule, not an ASCII-only encoding rule. Preserve the SDK's
  Unicode support and the exact semantics of protocol payloads, user input and test data;
  their identifiers, comments and explanatory text must still follow the English rule.
- Review changed files for language compliance before merging. Translate non-English
  engineering text in files being changed instead of carrying it forward. For an imported
  PR, ask the author to make the correction in the source branch, then import the new SHA;
  do not rewrite the author's commits during intake or rewrite historical Git commits for translation.

### Execution environments and evidence

- GitHub CI provides early contributor feedback. Preserve the existing Ubuntu/macOS
  coverage. Run fork PRs on GitHub-hosted runners with minimal read-only permissions and
  no repository secrets. Never execute PR code in a privileged `pull_request_target` context.
- A verified GitLab pipeline is the final CI evidence for merging. A green GitHub check
  does not replace it. The current `.gitlab-ci.yml` mainly defines branch push pipelines;
  do not treat MR pipelines as already enabled. Check the actual pipeline source, project,
  required jobs and `H`, rather than relying on any green indicator on the page.
- Execute external code in an isolated, temporary environment without release credentials
  or privileged internal-network access. Do not expose merge/sync keys, group secrets,
  `CI_JOB_TOKEN` or Git configuration containing credentials to the execution process.
  Keep checkout credentials on the trusted controller side. A fixed YAML file does not
  isolate executable CMake, test or npm code.
- Keep external MRs in Draft until the isolation setup is verified. Manual tests in a
  separate temporary VM provide preliminary or supplementary evidence; they do not bypass
  `Pipelines must succeed`. After enabling isolated CI in the controlled fork, verify that
  the canonical MR recognizes a valid pipeline for `H`. Administrators should disable
  `ci_allow_fork_pipelines_to_run_in_parent_project` and verify its effect. Do not casually
  run external fork pipelines in the parent project; `ci.skip` is not an isolation boundary.
- The current `pages` job runs only on `main/master`. Build documentation locally first.
  Subsequently separate MR documentation builds from protected-branch deployment, with
  GitLab as the sole official deployment source. Review and verify new MR rules or CI
  configuration changes separately.

### SDK validation requirements

- Follow the module's `CONTEXT.md` and its public API, PAL, threading and logging constraints.
  Check both root CMake and ESP-IDF source lists for every new `.c` file. Test registration
  rules and exceptions are documented in [AGENTS.md](AGENTS.md).
- For C changes, run the baseline build and full CTest suite. Add sanitizers, examples and
  target-platform validation according to the impact:

  ```bash
  git submodule update --init --recursive
  python3 -m pip install jsonschema jinja2 pycryptodome cryptography
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE="$(command -v python3)"
  cmake --build build -j 4
  ctest --test-dir build --output-on-failure --no-tests=error --timeout 180
  ```

- Run `ctest` serially and fail on zero tests. Isolate mock ports between jobs as well.
  Retain the FreeRTOS PAL header checks, ASan/UBSan and applicable valgrind checks. A passing
  leak script does not establish that test assertions passed. Report counts from the actual run.
- Build POSIX examples separately and confirm that the affected targets were produced.
  Record actual build/device validation for ESP-IDF changes.
- For `docs-site` changes, run `npm ci && npm run build` and check Chinese/English parity.
  For repository Markdown/template changes alone, check formatting, relative links and
  consistency with the guidelines.
- Record commands, exit results, passed/failed/skipped counts, the full pipeline URL and
  validation SHA in the MR. Missing, canceled or outdated runs, zero tests and missing
  required jobs do not count as passing evidence for the current revision.

## 6. Code review and merging

- Use the [GitLab MR template](.gitlab/merge_request_templates/Default.md). GitHub authors
  use the [PR template](.github/pull_request_template.md), which explains manual GitLab intake.
- Preserve public discussions, original review authors and conclusions in GitHub. Maintainers
  summarize outstanding items and source links in the GitLab MR. GitLab reviewers independently
  assess the final imported `H`; do not approve on behalf of a GitHub author under another identity.
- Require at least one reviewer other than the original author. The importing maintainer's
  identity does not replace the original author's identity. Public API, PAL, protocol,
  CI/build and protection-rule changes require review by the responsible module or maintenance owner.
- Reconfirm review after new commits or target-branch changes that affect the conclusions.
  An old Approve indicator remaining visible in Free is not approval of the new revision.
- Before merging, the maintainer verifies that `H` matches the source, current target `B`
  is an ancestor of `H`, required CI passed for `H`, review covers the current code,
  discussions are resolved, the repository language policy is satisfied,
  and references, CHANGELOG and migration instructions are complete.
  If target advancement invalidates the ancestry condition, the author merges the latest
  target branch and repeats validation. Do not temporarily bypass the merge settings.
- **Merge only in GitLab.** Record `M`, verify that `H` is its ancestor, and check the merged
  content. Close only issues whose acceptance criteria are satisfied. Preserve association
  records and necessary historical SHAs before deleting intake branches.
- Set the state to `Merged; awaiting sync` and hand over to the sync maintainer. Keep the
  GitHub PR's sync status visible until synchronization succeeds.

## 7. CHANGELOG and releases

- Record user-visible changes under `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md), grouped
  as `Added`, `Changed` or `Fixed`. Follow AGENTS.md exceptions for internal guidelines and
  build maintenance.
- Keep one line per item: `- <module> — <what changed> ([GitLab MR !N](full-URL)).`.
  For a GitHub-origin contribution, also retain the original PR's full link. Preserve the
  source of historical entries. Use sub-items only for necessary new symbols, defaults or
  migration steps; put motivation and validation details in commits or the MR.
- Use `tools/bump_version`. The version macro lives in `modules/iot-client/src/iot_internal.h`.
  The script edits only the header. Merge version and release-note changes through a GitLab
  MR, create an annotated `vX.Y.Z` tag from the validated release commit, push it to `origin`
  and create the GitLab Release.
- Sync each designated release tag to GitHub separately. Verify both the tag object SHA and
  its dereferenced commit SHA. The GitHub Release is a mirror using the same tag and commit;
  link the primary GitLab release record and provide a publicly readable change summary.
- Run `tools/bump_version next` through a new MR to start the next cycle. Resolve every
  pending reference in the release's scope. Use actual platform URLs for subsequent version
  comparison links and preserve historical links.

## 8. Manual sync and incident handling

- After merging, the sync maintainer fetches both official branches and confirms that the
  GitHub tip is an ancestor of the GitLab tip. Use a normal push for each explicit branch.
  Record `S`, before/after SHAs, time, operator and associated MRs for each batch.
  Do not use `git push --mirror`, force-push official branches, overwrite tags in bulk or
  routinely merge the GitHub official branch back into GitLab.
- Verify that both tips match and the synced tip contains `M` before closing the GitHub
  feedback loop. GitHub may recognize an indirect merge when the original commits remain
  intact. Otherwise, explain the actual GitLab merge, `M` and sync result, then close the PR.
  Never perform a second GitHub merge to obtain a purple merged indicator.
- New PR commits or a force push: freeze the ready-to-merge state, retain the old `H`,
  import the new revision, and repeat review and tests. Follow the runbook for non-fast-forward
  updates to unmerged intake branches only; this exception never applies to official branches.
- PR closure or withdrawal: check whether GitLab has already merged it. Close an unmerged
  canonical MR; preserve the record if it was merged. Revert code through a separate MR.
  Recheck associations and SHAs if the PR is reopened.
- GitLab merged but sync failed: retain `M` and `Merged; awaiting sync`. Fix network or
  permission issues and retry in the same direction. Do not merge again on either platform.
  Stop the affected sync on official-branch divergence or a tag conflict, and record the incident.
- Exception for divergence recovery: freeze sync and preserve both tips. Incorporate the
  original GitHub-only commits into GitLab history through isolated validation and a separate
  incident MR, then verify fast-forward sync is possible. Cherry-picking content alone does
  not restore ancestry. If retaining that history is unsafe, remain frozen and define a
  separate recovery plan. All official merges still go through GitLab MRs.
- Follow the [runbook](docs/development/manual-contribution-sync.md) for late CI results,
  stale approvals, duplicate MRs, source-branch deletion or disclosure of internal information.
  Suspend intake and releases for an affected official branch or release tag until recovery
  has been verified.

## 9. Rollout verification and future automation

- Maintainers create a rollout record listing actual GitHub/GitLab URLs, default branch,
  initial tips, controlled fork, owners, protection rules, isolation status, disposition of
  open PRs and the historical archive location.
- Verify that regular members cannot push directly or merge in GitHub, designated maintainers
  can merge in GitLab and fast-forward GitHub, and the GitHub sync bypass does not bypass
  force-push prohibitions. Maintainers must still manually reject unapproved MRs in Free.
- Exercise a trial PR through intake, cross-links, feedback to an author without GitLab
  access, CI and review for the correct SHA, merge, sync and closure. Test new commits,
  force pushes, withdrawal, interrupted sync, official-branch divergence and tag conflicts;
  retain the actual results.
- During this phase, maintainers manually check PR/MR updates and record their actions.
  Webhooks, Apps, a state database and automatic mirroring are not required. Review any
  future automation separately for signature validation, event deduplication/reconciliation,
  permission isolation, SHA binding and failure recovery. It must retain GitLab as the sole
  merge authority and sync official refs in one direction.

## References

- [GitLab 17.8.7: protected branches](https://gitlab.com/gitlab-org/gitlab-foss/-/blob/v17.8.7/doc/user/project/repository/branches/protected.md)
- [GitLab 17.8.7: approvals and tier differences](https://gitlab.com/gitlab-org/gitlab-foss/-/blob/v17.8.7/doc/user/project/merge_requests/approvals/index.md)
- [GitLab 17.8.7: merge methods](https://gitlab.com/gitlab-org/gitlab-foss/-/blob/v17.8.7/doc/user/project/merge_requests/methods/index.md)
- [GitLab 17.8.7: MR pipelines and fork execution](https://gitlab.com/gitlab-org/gitlab-foss/-/blob/v17.8.7/doc/ci/pipelines/merge_request_pipelines.md)
- [GitHub: rulesets and bypass permissions](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/creating-rulesets-for-a-repository)
- [GitHub: indirect merges](https://docs.github.com/en/pull-requests/reference/pull-request-merges#indirect-merges)
