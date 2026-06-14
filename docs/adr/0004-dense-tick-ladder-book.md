---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0004. Dense tick-ladder order book representation

## Context and Problem Statement

The book stores one FIFO per price level per side. The choice of data
structure for the price ladder dominates the latency of every hot-path
operation: add, cancel, walk-from-best, and modify.

## Decision Drivers

- O(1) add and cancel after price-to-tick conversion.
- O(1) best-bid / best-ask after each mutation.
- Linear walk from best when consuming a level (hardware prefetcher
  friendly).
- Memory budget on the order of tens of megabytes per book is acceptable.
- Must compose with the hierarchical bitmap (see
  [ADR-0005](0005-hierarchical-bitmap-best-price.md)).

## Considered Options

- Dense fixed-range tick array: `level levels[N_TICKS]` per side.
- Sparse intrusive set: `boost::intrusive::set<level>` keyed on tick.
- Hybrid: dense within +/-N ticks of mid, sparse beyond.
- Y-fast or van Emde Boas trie.

## Decision Outcome

Chosen option: **Dense tick-ladder array** per side, default
`N_TICKS = 1<<20` (~16 MiB per side at 16 B per level header). Tick
range and per-level FIFO type are template parameters so the same code
serves multiple instrument widths.

### Consequences

- Positive: Add and cancel are pointer arithmetic on the order's `px`.
- Positive: Walking from best is a tight loop over adjacent cache lines.
- Positive: No tree pointer chasing; no internal node cache misses.
- Negative: Upfront memory cost ~32 MiB per book.
- Negative: Instruments with unusually wide price ranges need either a
  larger `N_TICKS` (memory cost) or a follow-up hybrid ADR.
- Risk: Pricing changes at exchange level (tick-size shrink) may force
  a re-sizing migration.

## Pros and Cons of the Options

### Dense fixed-range tick array

- Pro: O(1) everywhere with simple, branch-prediction-friendly code.
- Pro: Hardware prefetcher loves linear access patterns.
- Pro: Pairs cleanly with a hierarchical bitmap for `best()` queries.
- Con: Memory footprint scales with tick range, not population.

### Sparse intrusive set

- Pro: Memory tracks population, not range.
- Pro: Handles unusually wide price ranges (FX, some crypto) without
  reconfiguration.
- Con: `O(log K)` best-price; comparisons; pointer chasing.
- Con: Cache miss on every node traversal.

### Hybrid (dense centre, sparse tails)

- Pro: Best of both for production books with thick mid-region.
- Con: More code paths; more state; complicates the matching kernel.
- Con: Tail population can spike during volatility events, defeating
  the assumption.

### Y-fast / vEB trie

- Pro: `O(log log U)` best-price.
- Con: Materially more complex; few reviewers will recognise the
  invariants.
- Con: Constant factors often beaten by the dense array for realistic
  populations.

## More Information

- Related: [ADR-0005](0005-hierarchical-bitmap-best-price.md).
- Related: [ADR-0006](0006-slab-arena-intrusive-fifo.md) (storage for
  the per-level FIFO entries).
