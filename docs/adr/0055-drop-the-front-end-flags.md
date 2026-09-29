---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0055. Drop the front end flags of the release build

## Context and Problem Statement

`cmake/CompilerFlags.cmake` adds `-fno-plt`, `-falign-functions=64` and
`-falign-loops=32` to release builds. Each changes the code both compilers emit,
and each acts on instruction fetch. The top-down method optimises a category
only once it is flagged (Yasin, ISPASS 2014, section 3). The flags belong in the
build when the front end limits the engine or the pipeline.

A first comparison built one binary a side and favoured the flags. One binary is
one sample from the space of layouts (Curtsinger and Berger, ASPLOS 2013,
section 1), and link order alone moved two SPEC programs by 4 and 2.6 per cent
(Kalibera and Jones, ISMM 2013, section 8.2).

## Decision Drivers

- A flag stays when a measurement on Friday shows it helps, and goes when none
  does.
- The engine's profiler and the load generator exercise different code, the
  first single threaded and the second across rings and threads.
- A difference between builds counts once it separates across randomised
  layouts, with the interval of Kalibera and Jones (ISMM 2013, equations 4 and
  5).

## Considered Options

- Keep the three flags.
- Drop them.

## Decision Outcome

Chosen option: **drop the three flags**. Across 30 binaries a side with
randomised layouts, the load generator's p50 at 0.3 of the saturated delivered
rate was 18 per cent lower without them, and no measure separated in their
favour.

The layout experiment of the workflow of ADR-0054 built `conda-clang-rel` and
`conda-clang-rel-no-frontend-flags` 30 times each, with `-ffunction-sections`
and lld's `--randomize-section-padding` given the binary's seed, and ran each
binary 30 times at loads 0 and 0.3 on the fc430 E5-2680 v4 nodes. bench_ci took
the mean of each binary's runs and reported the interval across binaries and the
Fieller interval of the ratio, at 95 per cent.

- At 0.3, the p50 without the flags over the p50 with them was 0.824 [0.792,
  0.856], means of 1640 and 1991 reference cycles. The p99 ratio, 1.852 [0.688,
  3.666], didn't separate.
- Closed, the p50 ratio was 0.966 [0.915, 1.019] and the p99 ratio 0.989 [0.930,
  1.050], neither separated.
- The interval of the mean p50 at 0.3 was 3.8 per cent of the mean without the
  flags and 0.8 per cent with them. The results of that run don't record the
  node, so they can't say whether the wider interval came from the binaries or
  from the nodes they ran on. Each result now opens with its node, and the
  target's `results/summary/layout/nodes.csv` groups the runs by node.

perf rated front end bound as good on every profiler workload with and without
the flags, from 2.1 to 13.5 per cent with them and from 2.1 to 15.0 per cent
without. Retiring moved by 1.7 points at most.

### Consequences

- Positive: the release build gives up three flags that no measurement supports,
  and the pipeline's p50 falls.
- Negative: the binaries measured were built with `-ffunction-sections`, which
  the release build doesn't. The effect is measured on a neighbouring build, and
  the three flags' shares of it are not separated.
- Neutral: the one binary a side of the first comparison fell on the other side.
  The layout experiment is the comparison the workflow repeats for a change of
  this kind.

## More Information

- ADR-0054 describes the workflow, the counter limit that restricts the top-down
  metrics to front end bound and retiring, and the offered loads.
- `results/summary/layout/{0,0.3}/{p50,p99}.txt` of the Friday run contain
  bench_ci's report, the source of every figure above.
- The build files that set the flags belong to the Conan migration in progress,
  which removes them.
