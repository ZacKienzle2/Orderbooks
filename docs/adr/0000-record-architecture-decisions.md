---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0000. Record architecture decisions

## Context and Problem Statement

Architectural decisions accrete silently as a codebase grows. Reviewers
and future contributors need the *why* behind each load-bearing choice
without trawling commit history or asking the original author.

## Decision Drivers

- Decisions must be discoverable in seconds.
- Reasoning must outlive the engineer who made the call.
- Reviewers must be able to challenge a single decision without
  rewriting an entire design document.
- Conventions should be paradigmatic so external reviewers recognise the
  format on sight.

## Considered Options

- Markdown Architecture Decision Records (MADR v3) under `docs/adr/`.
- Nygard-format ADRs (the original 2011 template) under `docs/adr/`.
- Monolithic design specifications under `docs/design/`.
- Decisions recorded only in commit messages and PR descriptions.

## Decision Outcome

Chosen option: **MADR v3 ADRs** under `docs/adr/`, one decision per
file, with file naming `NNNN-short-title-kebab-case.md` and a 4-digit
zero-padded sequence number.

### Consequences

- Positive: Every architectural decision has a permanent home with a
  stable URL.
- Positive: MADR v3 is the format used by ThoughtWorks, Spotify, Azure
  open-source repos; reviewers recognise it immediately.
- Positive: Superseding a decision is a first-class operation
  (`Status: Superseded by [ADR-NNNN]`).
- Negative: Discipline cost. Every meaningful decision needs an ADR;
  trivial ones do not, so authors must judge.
- Negative: Slight prose duplication between ADR consequences and
  README sections.

## Pros and Cons of the Options

### MADR v3

- Pro: Structured sections (Context, Decision Drivers, Considered
  Options, Decision Outcome, Pros and Cons) prompt complete thinking.
- Pro: Front-matter (`status`, `date`, `deciders`) machine-readable for
  index generation.
- Pro: Industry standard.
- Con: Slightly more ceremony than Nygard format.

### Nygard original

- Pro: Minimal: Title, Status, Context, Decision, Consequences.
- Con: No place for considered alternatives; reviewers must reconstruct.
- Con: Less recognisable to modern reviewers than MADR.

### Monolithic design docs

- Pro: One file to read for a feature.
- Con: Hides controversial decisions inside walls of supporting prose.
- Con: Cannot supersede individual decisions without rewriting the doc.
- Con: Diff review becomes harder as the doc grows.

### Commit messages only

- Pro: Zero process overhead.
- Con: Not discoverable without grep; not browseable by reviewers.
- Con: Loses the *considered options* context that ADRs preserve.

## More Information

- MADR project: <https://adr.github.io/madr/>
- Nygard, M. (2011). *Documenting Architecture Decisions*.
- Process documentation: [docs/adr/README.md](README.md).
