<!-- See CONTRIBUTING.md at the repository root. Replace guidance text, explain inapplicable items, and check only completed items. -->

## Problem and resulting behavior

<!-- Describe the concrete trigger, previous behavior and resulting behavior. -->

## Related records

- GitLab issue: full URL, or explain why no separate issue is needed.
- Source platform and actual author: GitHub / GitLab, author account; list the importer separately.
- Source GitHub PR: full URL with platform and number; use "N/A" for native GitLab MRs.
- Related historical PRs / MRs / design decisions: full URLs, or "None".
- Import fork / source branch:
- Merge maintainer / sync maintainer:

<!-- Keep the GitHub PR as the contributor communication entry point; merge only in GitLab. Cross-link both records and provide a public summary for an internal MR. -->

## Current revision and review

- Current source head SHA (H):
- Target SHA before merging (B):
- Reviewers, conclusions and corresponding SHA:
- Previous revision SHA and reason for updating:

## Implementation and compatibility

<!-- Explain the key implementation and affected modules/APIs/platforms; include migration steps for breaking changes. -->

## Validation

- Validated commit SHA:
- Commands, exit results and actual passed/failed/skipped counts:
- Full GitLab pipeline URL and corresponding commit SHA:
- Checks not run or skipped, and reasons:

<!-- Run ctest serially. Run npm run build locally for docs-site changes. For documentation-only changes, record formatting/link check results. -->

## Merge checklist

- [ ] This MR's title and every commit subject and body, including the merge commit message, are in English, including for docs-site/ and README.md changes.
- [ ] The change follows AGENTS.md and the relevant module's CONTEXT.md.
- [ ] Authored text follows the language policy, including Chinese explanations in root README.md examples and directory listings; comments in actual source, test, build and script files outside docs-site/ remain English.
- [ ] Validation appropriate to the impact is complete, and required CI jobs match the latest commit SHA.
- [ ] H matches the current source head, and the current target B is an ancestor of H.
- [ ] External code was validated in isolation; the MR recognizes a valid GitLab pipeline for H, without using privileged parent-project runners.
- [ ] Required public API, documentation, build lists and test registration updates are complete; inapplicable items are explained.
- [ ] SDK user-visible changes are recorded in CHANGELOG; internal changes are marked inapplicable with a reason.
- [ ] This change's CHANGELOG entries use this MR's full URL, with no pending numbers.
- [ ] Compatibility/migration notes are complete, and important design decisions are recorded in CONTEXT.md or an ADR.
- [ ] A non-author has reviewed the current revision, and required discussions are resolved (the merge maintainer checks this manually on Free).
- [ ] GitHub-origin changes retain the original PR link, squash is disabled, and source commits have not been rewritten in GitLab.

## Merge and manual sync record

- Status: In review / Awaiting author update / Ready to merge / Merged; awaiting sync / Synced; awaiting closure / Complete / Rejected/withdrawn.
- Final GitLab merge SHA (M):
- Sync branch, pre-sync GitHub SHA / synced tip SHA (S):
- Sync time, operator and tip comparison results for both platforms:
- Final GitHub PR state and public feedback URL (use "N/A" for native MRs):
- Release tag object SHA / peeled commit SHA (use "N/A" if no release is involved):
- Exceptions, actions taken and next responsible person:

<!-- Never merge again in GitHub. Sync official branches with ordinary fast-forward pushes and push only specified tags. On failure, retain "Merged; awaiting sync". -->
