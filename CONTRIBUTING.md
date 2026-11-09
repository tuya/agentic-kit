# Contributing guidelines (GitLab 17.8.7, manual phase)

Both GitHub PRs and GitLab MRs are contribution entry points. Each contribution has one
canonical GitLab MR for final review, validation and merging; all official merges, version
tags and releases originate in GitLab, and maintainers manually sync the merged results to
GitHub, which remains the public discussion channel and code mirror.

These guidelines target the CE/Free capabilities of GitLab 17.8.7 and require no paid
approval features, merge trains, bots or webhooks. Repository protection, CI isolation and
account permissions require separate verification. Maintainer procedures -- intake of
external PRs, contribution records, merging, syncing and incident handling -- live in the
[manual sync runbook](docs/development/manual-contribution-sync.md); this file carries the
policy, the roles and the rules every contributor must follow.

## Policy

- One canonical GitLab MR per contribution: search by the original PR URL before creating
  one, and update the existing MR for a follow-up of the same change.
- Final review and merging happen only in GitLab, preserving the author's original commit
  SHAs; imported commits are never rebased or squashed.
- Merges use `Merge commit with semi-linear history`; squashing is not allowed; `Pipelines
  must succeed` stays enabled. A verified GitLab pipeline for the exact source SHA is the
  merge evidence -- a green GitHub check does not replace it.
- At least one reviewer other than the author must approve. CE/Free does not enforce this;
  the merge maintainer checks it manually before merging.
- Public API, PAL, protocol, CI/build and protection-rule changes additionally require
  review by the responsible module or maintenance owner.
- Record design decisions in the module's `CONTEXT.md` or `docs/adr/` with source links;
  the canonical MR holds the contribution's validation and association records.

## Roles

- **Contributors** implement changes and complete their records (commits, validation
  evidence, CHANGELOG entries). External contributors work in GitHub forks and open PRs;
  members with GitLab access open MRs directly.
- **Module reviewers** review code against their module's domain terms
  ([CONTEXT-MAP.md](CONTEXT-MAP.md)).
- **Merge maintainers** perform the final acceptance checks and merge in GitLab.
- **Sync maintainers** import external PRs, sync official branches and tags to GitHub, and
  close the GitHub feedback loop. One maintainer may hold both merge and sync roles; each
  MR gets explicit merge and sync owners.
- Regular developers hold GitLab Developer and GitHub Read/Triage permissions. Maintainer
  access and the GitHub sync bypass are limited to designated maintainers.

Read [AGENTS.md](AGENTS.md) before making changes.

## Where to start

- Create `feature/<topic>`, `fix/<topic>`, `docs/<topic>` or `chore/<topic>` branches from
  the GitLab default branch, one independently reviewable objective per branch; small
  changes may record the problem, owner and acceptance criteria directly in the MR.
- External GitHub PRs are imported manually by a sync maintainer: keep pushing updates to
  your fork branch and keep the discussion on the PR; the import, validation and merge
  procedure is in the [runbook](docs/development/manual-contribution-sync.md).

## Commit requirements

- **Every PR/MR title and every new commit message must be in English** -- subjects, bodies
  and merge commit messages -- even for changes limited to `docs-site/` or the Chinese
  `README.md`.
- Use Conventional Commits, for example `feat(iot-client): ...`, `fix(tuya-ble): ...`,
  `docs: ...` or `chore: ...`; mark breaking changes with `!` and explain the migration.
- The body explains the reason for the change, links the source (full URL in plain-text
  commit messages), and ends with actual validation results and counts; note checks that
  were not run. Agent-authored commits include a `Co-Authored-By:` trailer.
- Preserve published history: an unmerged commit message is corrected by its author on the
  source branch, then re-imported; importers never rewrite source commits (the runbook
  covers the update flow).

## Language and code comments

- **English is the required authoring language, with two file-content exceptions:**
  `docs-site/` supports multilingual content, and the root `README.md` uses Chinese prose
  (`README_en.md` is the English version). The Chinese README may also use Chinese
  explanatory comments in documentation examples and directory listings.
- English applies to: identifiers and names (functions, types, variables, macros, enum
  values, files, directories), following the domain terms of the relevant `CONTEXT.md`;
  comments in source, test, build and script files (inline, block, file headers,
  Doxygen/API, docstrings, TODO/FIXME); documentation, ADRs, CHANGELOG entries, templates,
  scripts and build/CI configuration comments; logs, errors, diagnostics, test names and
  assertion explanations; and all contribution records (commit subjects and bodies, PR/MR
  titles and descriptions, review comments) -- contribution records stay English even when
  the changes only affect `docs-site/` or the Chinese `README.md`.
- This is an authoring-language rule, not an ASCII-only rule: preserve the SDK's Unicode
  support and the exact semantics of protocol payloads, user input and test data.
- Review changed files for language compliance before merging; translate non-English
  engineering text in files being changed instead of carrying it forward. For an imported
  PR, ask the author to correct the source branch, then import the new SHA.

## Validation before you submit

- Follow the module's `CONTEXT.md` and its public API, PAL, threading and logging
  constraints. Check both root CMake and the ESP-IDF source lists for every new `.c` file.
  Test registration rules are documented in [AGENTS.md](AGENTS.md).
- For C changes, run the baseline build and the full CTest suite:

  ```bash
  git submodule update --init --recursive
  python3 -m pip install jsonschema jinja2 pycryptodome cryptography
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE="$(command -v python3)"
  cmake --build build -j 4
  ctest --test-dir build --output-on-failure --no-tests=error --timeout 180
  ```

  Run `ctest` serially and fail on zero tests -- mock ports are compile-time constants
  shared across suites. Add sanitizer, examples, valgrind and target-platform validation
  according to the impact (AGENTS.md documents the module test layout).
- Build the POSIX examples separately and confirm the affected targets were produced;
  record actual device validation for ESP-IDF changes.
- For `docs-site` changes, run `npm ci && npm run build` and check Chinese/English parity.
- Record commands, exit results, passed/failed/skipped counts, the pipeline URL and the
  validated SHA in the MR. Missing, canceled or outdated runs, zero tests and missing
  required jobs do not count as passing evidence for the current revision.

## CHANGELOG and releases

- Record user-visible changes under `[Unreleased]` in [CHANGELOG.md](CHANGELOG.md), grouped
  as `Added`, `Changed` or `Fixed`; follow the AGENTS.md exceptions for internal guidelines
  and build-maintenance work.
- One line per item: `- <module> — <what changed> ([GitLab MR !N](full-URL)).` Use indented
  sub-items only for a new symbol, a changed default or a migration step; motivation and
  validation detail belong in the commit or MR, not the release notes.
- Use `tools/bump_version` (`show` / `next` / `release`); the version macro lives in
  `modules/iot-client/src/iot_internal.h`. Version and release-note changes merge through
  a GitLab MR, and the annotated `vX.Y.Z` tag is created from the validated release commit.
  Resolve every pending MR reference in a release's scope before publishing; tag syncing
  and the GitHub release mirror are maintainer operations (runbook).

## Cross-platform references

- Reference contributions by platform, record type, number and full URL; use Markdown
  links where supported: `[GitHub PR #47](https://github.com/tuya/agentic-kit/pull/47)`,
  `[GitLab MR !12](https://gitlab.example.com/group/project/-/merge_requests/12)`.
- In GitLab, `#N` identifies an issue and `!N` an MR; numbers are independent across
  platforms -- never mechanically replace a historical `#N` with `!N`, and never infer an
  MR number from a PR number. Existing bare `(#N)` references in CHANGELOG identify GitHub
  PRs in `tuya/agentic-kit`.
- Link each GitHub PR and its canonical GitLab MR in both directions (write `N/A` in the
  source field for GitLab-origin work). Public summaries for contributors without GitLab
  access must stay actionable without copying credentials, private logs or restricted
  discussions.

## Maintainer operations

Manual intake of external PRs (including the controlled fork and CI isolation),
contribution records and their states, the merge checklist, publishing mainlines and tags
to GitHub, closing the PR loop, incident handling (force pushes, divergence, failed sync)
and rollout rehearsal live in the
[manual sync runbook](docs/development/manual-contribution-sync.md). Historical merged or
closed GitHub PRs keep their platform, number, URL and discussion archived; they need no
replacement MRs. GitLab and GitHub protection settings, including the sync-bypass caveat,
are listed in the runbook's one-time setup section.

## References

- [GitLab 17.8.7: protected branches](https://gitlab.com/gitlab-org/gitlab-foss/-/blob/v17.8.7/doc/user/project/repository/branches/protected.md)
- [GitLab 17.8.7: approvals and tier differences](https://gitlab.com/gitlab-org/gitlab-foss/-/blob/v17.8.7/doc/user/project/merge_requests/approvals/index.md)
- [GitLab 17.8.7: merge methods](https://gitlab.com/gitlab-org/gitlab-foss/-/blob/v17.8.7/doc/user/project/merge_requests/methods/index.md)
- [GitLab 17.8.7: MR pipelines and fork execution](https://gitlab.com/gitlab-org/gitlab-foss/-/blob/v17.8.7/doc/ci/pipelines/merge_request_pipelines.md)
- [GitHub: rulesets and bypass permissions](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/creating-rulesets-for-a-repository)
- [GitHub: indirect merges](https://docs.github.com/en/pull-requests/reference/pull-request-merges#indirect-merges)
