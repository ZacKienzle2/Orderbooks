---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0010. Conventional Commits with git-cliff-generated CHANGELOG

## Context and Problem Statement

Commit history must be legible to reviewers, automatable for versioning
and changelog generation, and stable enough that release notes can be
produced without human intervention.

## Decision Drivers

- One canonical commit format across all contributors.
- Automatable CHANGELOG without per-PR manual entries.
- SemVer bumps inferable from commit history.
- Release notes generated reproducibly at tag time.
- ASCII-only commit messages per project policy.

## Considered Options

- Conventional Commits 1.0.0 + git-cliff for CHANGELOG.
- Conventional Commits 1.0.0 + commit-and-tag-version
  (formerly standard-version, Node-based).
- Conventional Commits 1.0.0 + cocogitto.
- Free-form commit messages + hand-written CHANGELOG entries on each PR.
- Free-form commit messages + release-drafter only.

## Decision Outcome

Chosen option: **Conventional Commits 1.0.0** enforced via
`commitlint.config.cjs` + `conventional-pre-commit` hook +
`.github/workflows/commitlint.yml`, with **`git-cliff`** as the
changelog generator. Configuration in `cliff.toml`. `CHANGELOG.md` is
treated as a generated artefact and is regenerated on every push to
`main` (via PR) and at every release tag.

### Consequences

- Positive: One commit format, enforced at three layers (pre-commit,
  commit-msg hook, CI).
- Positive: CHANGELOG is always in sync with commit history.
- Positive: SemVer bumps follow mechanically from `feat:` (minor),
  `fix:` (patch), and `!` / `BREAKING CHANGE:` footers (major).
- Positive: git-cliff is a single static binary (Rust); no Node dep,
  no Python dep, no GitHub Actions plugin lock-in.
- Negative: Contributors must learn the Conventional Commits format
  (low cost; tooling enforces it).
- Negative: Editing CHANGELOG by hand is forbidden; documented in
  CONTRIBUTING.md and enforced by the regeneration workflow.

## Pros and Cons of the Options

### Conventional Commits + git-cliff

- Pro: Industry-standard commit format.
- Pro: git-cliff: Rust binary, deterministic output, configurable
  templates.
- Pro: Native conventional-commits parser, breaking-change handling,
  scope grouping.
- Pro: Integrates with GitHub Actions via `orhun/git-cliff-action`.

### commit-and-tag-version (Node)

- Pro: Mature, widely used in JS ecosystems.
- Con: Node toolchain dependency in a C++ project is friction.
- Con: Slower; less deterministic templating than git-cliff.

### cocogitto

- Pro: Rust, similar to git-cliff.
- Pro: Also enforces conventional commits at commit time.
- Con: Smaller community than git-cliff.
- Con: Opinionated about repo layout in ways that conflict with
  vcpkg manifest mode.

### Free-form + hand-written CHANGELOG

- Pro: Maximum flexibility.
- Con: Manual upkeep rots; CHANGELOG drifts from history within weeks.
- Con: No SemVer automation.

### Free-form + release-drafter only

- Pro: Release notes happen automatically on the GitHub side.
- Con: Notes live in releases, not in the repo file `CHANGELOG.md`.
- Con: PR labels become the source of truth instead of commit
  messages, splitting the truth.

## More Information

- Conventional Commits 1.0.0: <https://www.conventionalcommits.org/en/v1.0.0/>
- git-cliff: <https://git-cliff.org>
- Repo config: [cliff.toml](../../cliff.toml).
- CI workflow: [.github/workflows/changelog.yml](../../.github/workflows/changelog.yml).
