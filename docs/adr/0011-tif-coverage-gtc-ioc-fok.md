---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0011. Time-in-force coverage: GTC, IOC, FOK

## Context and Problem Statement

Real exchanges support many time-in-force (TIF) variants. The engine
must pick a subset that exercises the matching kernel realistically
without ballooning the surface area beyond what the current scope
requires.

## Decision Drivers

- Cover the common cases reviewers expect in a credible matching
  engine.
- Exercise both "rest" and "drop" paths through the kernel.
- Avoid TIFs that require book state the engine does not yet expose
  (pegged needs a top-of-book listener; GTD needs a clock).

## Considered Options

- GTC, IOC, FOK.
- GTC only.
- Full set including post-only and pegged.

## Decision Outcome

Chosen option: **GTC, IOC, FOK**.

- **GTC** (good-till-cancel): residual rests on the book until matched
  or cancelled.
- **IOC** (immediate-or-cancel): match what is available; drop the
  residual.
- **FOK** (fill-or-kill): pre-check available qty against the
  opposite-side aggregate; abort all if insufficient.

Post-only and pegged are out of scope for this decision and may be
introduced under their own ADRs once the engine has a top-of-book
listener seam.

### Consequences

- Positive: Three TIFs exercise the rest path (GTC), the drop path
  (IOC), and the precheck path (FOK).
- Positive: Reviewer expectation is satisfied.
- Positive: Property tests cover all three branches.
- Negative: Post-only requires an additional "would-cross" check; not
  hard, but a separate code path.
- Negative: Pegged requires reactive repricing on top-of-book changes;
  needs a listener seam the engine does not currently expose.

## Pros and Cons of the Options

### GTC + IOC + FOK

- Pro: Covers the three semantic shapes of TIF: rest, drop, atomic.
- Pro: No additional book state required.
- Pro: Reviewers expect at least these three.

### GTC only

- Pro: Smallest surface area.
- Con: Reviewers will note the absence of IOC and FOK immediately.
- Con: Kernel does not exercise the drop or precheck paths.

### Full set including post-only + pegged

- Pro: Production-realistic coverage.
- Con: Post-only is mechanical but adds a path.
- Con: Pegged requires reactive listener machinery that does not yet
  exist; designing it before the requirement settles produces a worse
  abstraction than waiting.

## More Information

- Related: [ADR-0008](0008-single-thread-engine-spsc-boundary.md)
  (matching loop where TIF dispatch happens).
- Related: [ADR-0012](0012-self-cross-policy-configurable.md) (TIF and
  self-cross interact on the cross path).
