---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0028. In-place relink for a resting price-move modify

## Context and Problem Statement

A modify that changes price forfeits time priority, so the engine implemented
it as a cancel of the old order followed by a resubmit at the new price. That
is correct but does redundant work when the order survives the move. The cancel
erases the id from the open-addressed index with a backward-shift loop and frees
the arena slot; the resubmit allocates a fresh slot and inserts the id again.
The order ends up with the same id and the same fields, so the slot churn and
the two hash operations accomplish nothing.

Per-operation profiling under a realistic random-access workload identified the
price-move modify as the costliest path, and the two hash operations and the
allocate-free pair as cache-missing steps on it. The question is whether the
move can keep the existing record.

## Decision Drivers

- The price-move modify is the most expensive engine operation under random
  flow, so it is where saved work matters most.
- A change must preserve observable behaviour exactly, including the loss of
  time priority and the coalesced top-of-book emission.
- A crossing price move must still match against the book, so any fast path is
  conditional on the new price resting.

## Considered Options

- Relink the existing record in place when the new price does not cross, and
  fall back to cancel-and-resubmit when it does.
- Keep cancel-and-resubmit unconditionally.
- Add a free-list cache to make the allocate-free pair cheaper without changing
  the algorithm.

## Decision Outcome

Chosen option: **relink in place when the new price rests**, because it removes
the index and arena work the move does not need while leaving behaviour
identical, with a fallback to the existing path when the move crosses.

When the new price does not cross the opposite best, on_modify unlinks the order
from its current level, sets the new price and quantity, and links it at the
back of the new level. The id_index entry and the arena slot are untouched
because the record survives, so the backward-shift erase, the probe insert, and
the allocate-free pair are all skipped. The bitmaps and level aggregates update
through the same add and remove primitives the cancel and submit paths use. A
price change still lands at the back of the new level, so time priority is lost
exactly as before. When the new price would cross, the engine keeps the cancel
and resubmit path so the order matches.

A differential harness ran an identical 60000-operation stream, heavy on
price-move modifies both resting and crossing, against the in-place engine and
the cancel-and-resubmit baseline. The two event streams were byte-identical,
confirming the fast path changes nothing observable.

An A/B over six million random resting price moves on a one-sided book, the
median of three runs, measured the in-place path against the baseline.

- Core cycles fell from about 1.81 billion to about 0.67 billion, a 2.7x
  reduction.
- Retired instructions fell from 2.67 billion to 1.23 billion, a 2.2x
  reduction, the backward-shift erase and probe insert being the bulk of it.
- L1 data-cache load misses fell about 12 percent, and instructions per cycle
  rose from 1.47 to 1.84.

### Consequences

- Positive: the common resting price move is about 2.7x cheaper in cycles and
  does roughly half the instructions.
- Positive: behaviour is unchanged, proven byte-for-byte by the differential
  harness, so existing tests still cover correctness.
- Positive: the path no longer churns the arena free list on a resting move,
  which keeps slot locality steadier under sustained modify flow.
- Negative: on_modify carries a second code path and a cross test, a small
  complexity cost over the single cancel-and-resubmit call.
- Negative: the crossing move still pays the full cancel-and-resubmit cost,
  which this change does not address.

## Pros and Cons of the Options

### Relink in place with a crossing fallback

- Pro: removes the index and arena work on the common move, behaviour identical.
- Con: two paths in on_modify instead of one.

### Keep cancel-and-resubmit

- Pro: one code path.
- Con: pays two hash operations and an allocate-free pair the move does not need,
  on the costliest engine operation.

### Free-list cache

- Pro: cheapens allocation without touching the algorithm.
- Con: leaves the redundant hash operations and addresses a symptom rather than
  the cause.

## More Information

- Implementation: `include/lob/engine.hpp`, `on_modify`.
- Benchmark: `bench/bench_engine.cpp`, `bench_modify_price` exercises the
  resting relink on a one-sided book.
- Method: this reversed the discipline gap that ADR-0027 recorded, shipping the
  change only after a differential proof and an A/B measurement.
- Related: ADR-0017 (open-addressed id index) and ADR-0006 (slab arena) for the
  structures the fast path now leaves untouched.
