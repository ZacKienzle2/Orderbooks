---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0005. Hierarchical bitmap for best-price queries

## Context and Problem Statement

Given a dense tick-ladder (see
[ADR-0004](0004-dense-tick-ladder-book.md)), every `add` and `erase`
needs `best_bid()` / `best_ask()` in constant, predictable time. A
naive linear scan of `level levels[N_TICKS]` would dominate latency for
sparse populations.

## Decision Drivers

- Best-price query in deterministic constant time, ideally a small
  number of CPU instructions.
- Sub-cache-line set / clear cost on each mutation.
- No data-dependent branches in the hot path.
- Composability with SIMD popcount when emitting snapshots.

## Considered Options

- Hierarchical bitmap with 64-bit words at each tier.
- Boost.Intrusive `set` of populated levels keyed on tick.
- Maintained `tick_t cursor_best_bid_, cursor_best_ask_` updated by
  inspection on each empty-level event.
- Segment tree on populated ticks.

## Decision Outcome

Chosen option: **Three-tier hierarchical bitmap of 64-bit words**.
`L0[Ticks/64]`, `L1[L0/64]`, `L2[L1/64]`. `highest_set()` walks
top-down with `__lzcnt_u64` (three operations). `set()` and `clear()`
update at most three words plus a population counter.

Add a fourth tier when `N_TICKS > 64^3 = 262 144`.

### Consequences

- Positive: Best-price queries in ~5 ns warm, deterministic, no
  branches.
- Positive: Set / clear cost is constant.
- Positive: Tier sizing is `constexpr`; the bitmap is a fixed-size
  member of the book, no allocation.
- Positive: AVX2 / AVX-512 popcount paths accelerate bulk snapshot
  emission.
- Negative: Memory overhead of three tiers (small: a few hundred bytes
  at default sizes).
- Risk: AVX-512 detection has to happen at runtime; the engine caches a
  function pointer so the dispatch cost amortises to zero in the hot
  path.

## Pros and Cons of the Options

### Hierarchical bitmap

- Pro: O(1) `best()`, O(1) `set()` / `clear()`.
- Pro: Cache-friendly: top tier is a single 64-bit word for small `N`.
- Pro: SIMD-friendly for bulk operations.
- Con: Custom code; reviewers need a moment to internalise the tier
  arithmetic.

### Intrusive set

- Pro: Already used by Boost.Intrusive elsewhere in the codebase.
- Con: O(log K) best-price with pointer chasing.
- Con: Cache miss on each node traversal under sparse populations.

### Maintained best cursor

- Pro: Zero cost on `add` if price is the new best.
- Con: On `erase` of the current best, must scan downward to find the
  next populated level; worst case linear in tick range.

### Segment tree

- Pro: O(log N) best-price.
- Pro: Supports range aggregates.
- Con: Slower than the bitmap for `best()`.
- Con: More memory.
- Con: More complex; reviewers will question whether the extra range
  aggregate matters in this domain.

## More Information

- Related: [ADR-0004](0004-dense-tick-ladder-book.md).
- Reference: Lemire, D. (2018). "Fast bitmap operations." Software:
  Practice and Experience.
