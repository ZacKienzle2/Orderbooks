---
status: "Accepted"
date: "2026-06-19"
deciders: ["Zac Kienzle"]
---

# 0025. Software prefetch and invariant hoist in the match sweep

## Context and Problem Statement

When an aggressor crosses the opposite best, the engine walks that price
level's intrusive FIFO from the front, consuming maker orders until the
aggressor is filled or the price stops crossing. Each step reads the front
order, emits its fill, and on a full fill unlinks it and advances to the
successor. Reading the successor follows an intrusive list pointer into an
arena slot that the hardware prefetcher cannot predict, because freelist reuse
scrambles the address order of orders relative to their FIFO order. On a deep
single-level sweep every fill therefore risks a cold cache miss on the next
order's 64-byte line, and the miss latency serialises against the fill work
rather than overlapping it.

A second, smaller cost sits in the same loop. The self-cross guard tests the
aggressor's account against each maker's account. The aggressor's account is
fixed for the whole sweep, but the maker writes inside the loop body stop the
compiler from proving that the aggressor record is untouched, so it reloads the
aggressor's account field on every fill.

## Decision Drivers

- The match sweep is the hottest path under crossing flow and the one that
  walks unpredictable pointers, so it is where a memory stall hurts most.
- Any change must be behaviour-preserving. Matching stays strict price-time
  priority with identical fills, so the optimisation is a pure timing hint plus
  a hoist of a value the loop already computes.
- The technique must match the patterns already in the file, so the cancel and
  modify paths and the sweep read the same way.

## Considered Options

- A software prefetch of the successor order issued one fill ahead, paired with
  hoisting the self-cross invariant out of the inner loop.
- Restructuring the order pool so FIFO order matches address order, letting the
  hardware prefetcher stream the sweep.
- Leaving the sweep as a plain pointer walk and relying on the hardware
  prefetcher.

## Decision Outcome

Chosen option: **prefetch the successor one fill ahead and hoist the self-cross
invariant**, because it hides the pointer-chase latency with a single hint that
costs nothing when the line is already resident, keeps matching behaviour
byte-for-byte identical, and reuses the prefetch idiom the cancel and modify
paths already follow.

The inner loop binds the front order through the FIFO iterator, then reads the
successor iterator and issues a write-intent prefetch for the successor's line
before computing the current fill. The successor pointer lives in the front
order's already-resident line, so reading it adds no miss. The hint uses
write-intent locality because the upcoming pop rewrites the successor's
intrusive previous-link, so the line is wanted in Modified state and the
read-for-ownership upgrade is skipped, the same reasoning the on_cancel path
documents. The self-cross test becomes a single boolean computed once before
the outer loop, so the sweep no longer reloads the aggressor account per fill.

### Consequences

- Positive: the successor's line is fetched while the current fill is computed,
  so a deep sweep overlaps memory latency with work instead of stalling.
- Positive: matching behaviour is unchanged, so the existing differential and
  invariant tests cover correctness without new cases.
- Positive: the inner loop drops one account-field reload per fill.
- Negative: the prefetch is wasted work when the level is shallow or the
  successor is already cached, though an unused prefetch is far cheaper than a
  missed one.
- Negative: the loop now depends on the FIFO iterator exposing the successor,
  which couples it slightly more tightly to the intrusive list type.

## Pros and Cons of the Options

### Prefetch plus invariant hoist

- Pro: hides the dominant stall with a one-line hint, preserves behaviour, and
  matches the existing prefetch idiom.
- Con: adds a prefetch that the shallow-level case does not need.

### Reorder the pool to match FIFO order

- Pro: would let the hardware prefetcher stream the sweep with no software hint.
- Con: defeats freelist reuse and its locality on the allocate and cancel
  paths, trading a sweep win for a regression everywhere else.

### Plain pointer walk

- Pro: simplest possible loop.
- Con: leaves the sweep stalling on an unpredictable miss per fill, the exact
  cost this engine exists to avoid.

## More Information

- Implementation: `include/lob/engine.hpp`, `match_against_opposite_`.
- Benchmark: `bench/bench_engine.cpp`, `bench_match_deep_sweep` rests a tall
  single-price FIFO and times an aggressor that consumes the whole stack, so
  the measured work is dominated by the sweep's pointer-chasing.
- Related: ADR-0006 (slab arena and intrusive FIFO) for the layout the sweep
  walks, and the on_cancel and on_modify prefetch hints in the same header.
