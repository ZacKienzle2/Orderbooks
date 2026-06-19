---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0029. Guard the top-of-book recompute behind a price test

## Context and Problem Statement

Every mutating operation ends by calling publish_top_if_changed_, which reads
both sides' best price from the bitmap and both best levels' aggregate quantity,
compares the four values against the last published top, and emits a top_msg
when they differ. Each mutation marked the top dirty unconditionally, so the
recompute ran on every submit, cancel, and modify even when the operation
touched a level far from the top and could not have changed it.

Profiling stubbed the recompute and re-ran a cancel-and-submit workload. The
recompute accounted for about a quarter of the cycles and instructions on that
path, almost all of it wasted on operations that left the top unchanged. The
question is how to skip the recompute when an operation cannot move the top,
without missing a top that did change.

## Decision Drivers

- The recompute is on every order, so removing it from the common case helps the
  whole engine, not one operation.
- A skipped recompute must never drop a top_msg that should have been emitted,
  so the test must be conservative.
- The throttle-off mode emits a top per mutation by contract, so the guard must
  not change that.

## Considered Options

- Mark the top dirty only when the mutated price is at or beyond the side's
  current best, or the side was previously empty.
- Maintain the best price and quantity incrementally on every add and remove.
- Leave the unconditional recompute in place.

## Decision Outcome

Chosen option: **mark the top dirty only when the mutation can move it**,
because it removes the recompute from the common case with a single comparison
and no new incremental bookkeeping.

The engine caches the last published best price per side and a flag for whether
each side held any order at that top. A mutation at price px on side s can move
the top only when no top is established yet, the side was empty at the last top,
or px is at or beyond the side's best, that is px >= best_bid for a bid or
px <= best_ask for an ask. A change strictly worse than the best on a non-empty
side leaves both the best price and the best level's quantity untouched, so it
is skipped and the recompute never runs. A match always consumes the best level,
so the match and self-cross paths keep marking the top unconditionally. The
cached best stays current because the test is conservative; every operation that
could move the top sets the flag, runs the recompute, and refreshes the cache.

The guard applies only when the top throttle is on. With it off the engine emits
a top per mutation by contract, so mark_top_ then sets the flag unconditionally.

A differential harness ran a 60000-operation stream, heavy on price-move
modifies, cancels, and crosses, against the guarded engine and the prior one,
under both throttle settings. Every event stream was byte-identical, so the
guard drops no top and adds none. An A/B over a cancel-and-submit workload
measured the guarded engine against the prior one.

- Core cycles per operation fell about 24 percent.
- Retired instructions fell about 22 percent, the per-operation best recompute
  being the removed work.
- The crossing path was unchanged, as expected, since a cross always moves the
  top.

### Consequences

- Positive: every non-top submit, cancel, and modify skips two bitmap descents
  and two aggregate reads, cutting roughly a quarter of the cycles on that path.
- Positive: behaviour is unchanged under both throttle settings, proven
  byte-for-byte by the differential harness.
- Positive: no incremental best bookkeeping; the cache is refreshed only when
  the recompute already runs.
- Negative: on_modify and the other mutators carry a small price test, and the
  hot state holds two more flags.
- Negative: the guard depends on the cached best staying consistent with the
  book, an invariant the conservative test maintains but that future mutators
  must respect by marking the top when in doubt.

## Pros and Cons of the Options

### Guard the recompute behind a price test

- Pro: removes the recompute from the common case with one comparison, no new
  bookkeeping, behaviour identical.
- Con: a new invariant that every mutator must mark the top when a move is
  possible.

### Maintain best price and quantity incrementally

- Pro: the top is always known without any recompute.
- Con: every add and remove updates the cached best, and a removal at the best
  must find the next best, which is the bitmap descent the guard avoids paying
  on the common case.

### Leave the unconditional recompute

- Pro: simplest, one code path.
- Con: spends about a quarter of the per-order cycles recomputing a top that
  usually did not change.

## More Information

- Implementation: `include/lob/engine.hpp`, `affects_top_`, `mark_top_`, and the
  mutation sites in `on_cancel`, `on_modify`, and `rest_`.
- Method: shipped only after a differential proof under both throttle settings
  and an A/B, the discipline ADR-0027 set.
- Related: ADR-0005 (hierarchical bitmap) for the best query the guard avoids,
  and ADR-0024 latency measurement for the percentiles this should improve.
