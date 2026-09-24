---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0000. Record architecture decisions

## Context and Problem Statement

Architectural decisions accrete silently as a codebase grows. Reviewers and
future contributors need the reasoning behind each choice the design depends on
without trawling commit history or asking the original author.

## Decision Drivers

- Decisions must be discoverable in seconds.
- Reasoning must remain readable after the engineer who made the call leaves.
- Reviewers must be able to challenge one decision without rewriting an entire
  design document.
- Conventions should follow a published standard, so external reviewers
  recognise the format on sight.

## Considered Options

- Markdown Architecture Decision Records (MADR v3) under `docs/adr/`.
- Nygard-format ADRs (the original 2011 template) under `docs/adr/`.
- Monolithic design specifications under `docs/design/`.
- Decisions recorded only in commit messages and PR descriptions.

## Decision Outcome

Chosen option: **MADR v3 ADRs** under `docs/adr/`, one decision per file, with
file naming `NNNN-short-title-kebab-case.md` and a 4-digit zero-padded sequence
number.

### Consequences

- Positive: a recorded decision has a permanent home with a stable URL.
- Positive: MADR v3 is the format used by ThoughtWorks, Spotify and Azure
  open-source repos. Reviewers recognise it immediately.
- Positive: superseding a decision is a first-class operation
  (`Status: Superseded by [ADR-NNNN]`).
- Negative: discipline cost. A decision that later work builds on needs an ADR
  and a trivial one does not, so authors must judge.
- Negative: slight prose duplication between ADR consequences and README
  sections.

## Pros and Cons of the Options

### MADR v3

- Pro: its sections for context, drivers, options, outcome and trade-offs prompt
  complete thinking.
- Pro: front matter (`status`, `date`, `deciders`) is machine-readable for index
  generation.
- Pro: industry standard.
- Con: slightly more ceremony than Nygard format.

### Nygard original

- Pro: minimal, with a title, status, context, decision and consequences.
- Con: the format has no place for considered alternatives. Reviewers must
  reconstruct them.
- Con: less recognisable to modern reviewers than MADR.

### Monolithic design docs

- Pro: one file to read for a feature.
- Con: hides controversial decisions inside walls of supporting prose.
- Con: cannot supersede individual decisions without rewriting the doc.
- Con: diff review becomes harder as the doc grows.

### Commit messages only

- Pro: zero process overhead.
- Con: finding a decision takes grep, and reviewers cannot browse them.
- Con: loses the _considered options_ context that ADRs preserve.

## More Information

- MADR project: <https://adr.github.io/madr/>
- Nygard, M. (2011). _Documenting Architecture Decisions_.
- Process documentation: [docs/adr/README.md](README.md).
