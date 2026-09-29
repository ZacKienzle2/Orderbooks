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
randomised layouts, the load generator's p50 was lower without them, by 3.5 per
cent closed and 21 per cent at 0.3 of the saturated delivered rate.

The layout experiment of the workflow of ADR-0054 built `conda-clang-rel` and
`conda-clang-rel-no-frontend-flags` 30 times each, with `-ffunction-sections`
and lld's `--randomize-section-padding` given the binary's seed, and ran each
binary 30 times at loads 0 and 0.3 on the fc430 E5-2680 v4 nodes. bench_ci took
the mean of each binary's runs and reported the interval across binaries and the
Fieller interval of the ratio, at 95 per cent.

- Closed, the p50 without the flags over the p50 with them was 0.965 [0.961,
  0.970], and the p99 0.987 [0.984, 0.990].
- At 0.3, the p50 ratio was 0.792 [0.786, 0.799], means of 1577 and 1991
  reference cycles. The p99 ratio, 1.105 [0.457, 2.141], didn't separate.
- The saturated phase delivered a median of 54.7 million orders a second on both
  sides, and the two builds were offered the same absolute rate at 0.3. The
  generator's median lag was 83 cycles with the flags and 64 without.
- Every run whose saturated phase fell to about 26 million orders a second ran
  on smp-9-18, which took 10 of the 120 binary and load pairs. The figures above
  leave those 10 out. With them, the p50 ratio at 0.3 was 0.824 [0.792, 0.856]
  and the closed ratios didn't separate.
- On each other node that ran both builds at a load, the build without the flags
  had the lower median p50.

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
- `results/summary/layout/{0,0.3}/{p50,p99}.txt` of the run contain bench_ci's
  report over every node. The controller's log and `sacct` place each binary and
  load on its node.
- The build files that set the flags belong to the Conan migration in progress,
  which removes them.
