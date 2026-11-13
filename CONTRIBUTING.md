# Contributing to agentic-kit

Thanks for helping improve agentic-kit! Code, docs, bug reports, and questions
are all welcome.

Both **GitHub PRs** and **GitLab MRs** are accepted. All review and merging
happen in **GitLab**, where each contribution is tracked by one canonical MR.
A GitHub PR stays the discussion thread and is **closed — not merged — on
GitHub** once the change ships from GitLab in a release; your commits and
authorship are preserved.

## Before you start

Read [AGENTS.md](AGENTS.md) and the relevant module's `CONTEXT.md`
(see [CONTEXT-MAP.md](CONTEXT-MAP.md)) — they define the architecture,
conventions, and domain language this project expects.

## Making a change

- Branch from the default branch (`master`): `feature/…`, `fix/…`, `docs/…`,
  or `chore/…`.
- Author code, comments, commit messages and PR/MR titles in **English**
  (only exceptions: multilingual `docs-site/` content and Chinese prose in the
  root `README.md`). Use [Conventional Commits](https://www.conventionalcommits.org/)
  with a module scope, e.g. `feat(iot-client): …`; mark breaking changes with `!`.
- Validate before submitting, and report the commands and actual test counts:
  ```sh
  git submodule update --init --recursive
  cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE="$(command -v python3)"
  cmake --build build -j4
  ctest --test-dir build --output-on-failure --no-tests=error --timeout 180
  ```
- For any user-visible change, add a `## [Unreleased]` entry to
  [CHANGELOG.md](CHANGELOG.md) with the PR/MR link.

## Submitting

Open a GitHub PR or a GitLab MR and fill in the template. Push follow-up changes
to the **same branch** so maintainers can re-sync the exact revision. Review and
merge are done by maintainers in GitLab.

## Maintainers

GitHub→GitLab intake, one-way sync, releases, and repository-protection
settings are documented in the
[manual contribution sync runbook](docs/development/manual-contribution-sync.md).
