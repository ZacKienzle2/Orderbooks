---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0055. Keep the front end flags of the release build

## Context and Problem Statement

`cmake/CompilerFlags.cmake` adds `-fno-plt`, `-falign-functions=64` and
`-falign-loops=32` to release builds. Each changes the code both compilers emit,
and each acts on instruction fetch. The top-down method optimises a category
only once it is flagged (Yasin, ISPASS 2014, section 3). The flags belong in the
build when the front end limits the engine or the pipeline.

## Decision Drivers

- A flag stays when a measurement on Friday shows it helps, and goes when none
  does.
- The engine's profiler and the load generator exercise different code, the
  first single threaded and the second across rings and threads.

## Considered Options

- Keep the three flags.
- Drop them.

## Decision Outcome

Chosen option: **keep the three flags**. With them the load generator's
open-loop latency was lower at both offered loads, and the closed phase matched
within its spread. The profiler workloads stayed below the front end threshold
of perf on both sides.

The workflow of ADR-0054 built `conda-clang-rel` and
`conda-clang-rel-no-frontend-flags`, which presets the check results of the
three flags to false, and measured both on the fc430 E5-2680 v4 nodes.

- perf rated front end bound as good on every profiler workload with and without
  the flags, from 2.1 to 13.5 per cent with them and from 2.1 to 15.0 per cent
  without. Retiring moved by 1.7 points at most.
- Closed, the load generator's p50 over 30 runs had a median of 810 reference
  cycles with the flags and 813 without.
- At 0.1 of the saturated delivered rate, the median p50 was 958 with the flags
  and 978.5 without, and the median p99 2425 and 2556, whose interquartile
  ranges didn't overlap.
- At 0.3, the median p50 was 1590 with the flags and 1956.5 without, and the
  median p99 5603 and 9095. The interquartile ranges didn't overlap, and the
  generator's mean lag stayed below 80 cycles on both sides.

### Consequences

- Positive: the pipeline keeps its lower latency under open-loop load.
- Negative: one binary was built on each side. Link order alone moved two SPEC
  programs by 4 and 2.6 per cent (Kalibera and Jones, ISMM 2013, section 8.2),
  which is below the difference at 0.3 but leaves the flags' share of it
  unmeasured. Binaries relinked with lld's `--shuffle-sections` would measure
  it.
- Neutral: the profiler workloads don't show the difference. It arises in code
  they don't run, which includes the rings and the threads of the pipeline.

## More Information

- ADR-0054 describes the workflow, the counter limit that restricts the top-down
  metrics to front end bound and retiring, and the offered loads.
- `results/summary/topdown.csv` and `results/summary/latency.csv` of the run
  hold the figures above.
