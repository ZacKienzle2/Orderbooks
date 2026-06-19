---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0027. Match-sweep prefetch measured and reverted

## Context and Problem Statement

ADR-0025 added a software prefetch of the successor order to the match sweep,
on the premise that walking a price level's intrusive FIFO stalls on a cold
cache miss per fill that a prefetch issued one fill ahead would hide. It also
hoisted the self-cross test out of the inner loop. The change shipped without a
before-and-after measurement, justified by mechanism alone.

A benchmark now exists for exactly this path, so the premise is testable. The
question is whether the prefetch earns its place.

## Decision Drivers

- A perf change must show a measured win on a representative input, not a
  plausible mechanism.
- The common sweep input matters most, and a change that helps a rare pattern
  while slowing the common one is a net loss.
- The cost of carrying an unproven optimisation is paid on every match.

## Considered Options

- Keep the prefetch and the hoist as shipped.
- Revert the engine change and keep the deep-sweep benchmark that tests it.
- Keep only the self-cross hoist.

## Decision Outcome

Chosen option: **revert the engine change and keep the benchmark**, because an
A/B measurement shows the prefetch costs the common case and buys nothing on
the case it targets.

Two inputs were measured, each as the median of fifteen repetitions, building
the prefetch engine and the pre-change engine from the same benchmark source
so only the header differs.

- Contiguous deep sweep (`bench_match_deep_sweep`, a freshly built single-level
  FIFO whose arena slots are near-contiguous). Baseline 1833 ns, prefetch
  1900 ns, a 3 to 11 percent regression across runs. The slots are already
  streamed by the hardware prefetcher, so the software prefetch is pure
  overhead, an extra successor lookup and a redundant hint per fill.
- Scattered sweep (a target level interleaved with a random count of filler
  orders at other levels, so the FIFO walks the arena at irregular strides at
  roughly 73 cycles per fill). Baseline near 39000 cycles, prefetch near 38200,
  inside run-to-run noise. One fill of lookahead, about 73 cycles, cannot cover
  a last-level miss of a few hundred cycles, and a linked list cannot be
  prefetched several nodes ahead without chasing the very pointers the prefetch
  would hide.

The self-cross hoist measured as noise against the baseline, so it is reverted
with the prefetch to restore the proven-fastest loop rather than carry a
neutral change. The benchmark stays, because it is what made the regression
visible and it guards the path going forward.

### Consequences

- Positive: the match loop returns to its measured-fastest form on the common
  input.
- Positive: the deep-sweep benchmark remains, so a future sweep change is held
  to a measured standard.
- Positive: the episode sets the bar that a perf claim ships with an A/B, the
  same discipline the latency gate (ADR-0026) enforces in CI.
- Negative: the scattered-sweep miss latency is left unhidden, but it is an
  inherent cost of the intrusive-list layout (ADR-0006) that a one-ahead
  prefetch does not address.

## Pros and Cons of the Options

### Keep the prefetch and hoist

- Pro: no further change.
- Con: carries a measured regression on the common case for no gain elsewhere.

### Revert and keep the benchmark

- Pro: restores the fastest loop and retains the test that proved the point.
- Con: spends a second change to undo a one-day-old one.

### Keep only the hoist

- Pro: retains a defensible micro-cleanup.
- Con: a noise-neutral change muddies the revert for no measured benefit.

## More Information

- Supersedes ADR-0025.
- Implementation: `include/lob/engine.hpp`, `match_against_opposite_`, restored
  to its pre-prefetch form.
- Benchmark: `bench/bench_engine.cpp`, `bench_match_deep_sweep`.
- Related: ADR-0026 (latency-ceiling gate) for the measurement discipline this
  reversal applies.
