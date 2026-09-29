---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0056. Keep the profile-guided build off by default

## Context and Problem Statement

ADR-0051 adopted profile-guided optimisation as an opt-in pair of presets, and
counted the profile with cachegrind at 0.5 to 2.5 per cent fewer instructions
over the profiler's workloads. Instruction counts say nothing about the
pipeline's latency, which runs across rings and threads the profiler doesn't
exercise. A change on a branch trained the profile on the gateway and the load
generator as well. Should the release build read a profile, and should the
training include the applications?

## Decision Drivers

- The engine is judged on its end-to-end latency, closed and under open load.
- A build input that pessimises silently when stale needs a measured gain to
  justify it.

## Considered Options

- Keep the profile-guided build off by default, and keep the training on the
  profiler's workloads.
- Build releases from a profile trained on the profiler, the gateway and the
  load generator.

## Decision Outcome

Chosen option: **keep it off, and keep the training as it was**. Trained on the
profiler, the gateway and the load generator, the profile-guided build had the
higher p99 closed, and the higher p50 and p99 at both open loads.

The workflow of ADR-0054 built `conda-clang-rel` and `conda-clang-pgo`, trained
by `conda-clang-pgo-generate`, and ran each 30 times on the fc430 E5-2680 v4
nodes. The table gives the medians of the per-run percentiles in reference
cycles.

| Load   | p50, release | p50, profile | p99, release | p99, profile |
| ------ | -----------: | -----------: | -----------: | -----------: |
| Closed |          843 |          831 |         1074 |         1119 |
| 0.1    |          944 |         1008 |         2434 |         2563 |
| 0.3    |         1566 |         1705 |         5747 |         5807 |

The interquartile ranges of the closed p99 and of the p50 at 0.3 didn't overlap,
and those of the p50 at 0.1 met at their ends. The closed p50 was within its
spread. The profile-guided build also dropped more egress events in its
throughput phase, a median of 825,498 against 601,918.

The profiler's top-down metrics moved both ways. Retiring rose from 68.1 to 79.0
per cent on the cross workload and from 44.6 to 49.8 on the sweep, and front end
bound on cross rose from 4.3 to 10.6 per cent.

### Consequences

- Positive: the release keeps the lower latency, and the build has no profile to
  keep current.
- Negative: the throughput-bound profiler workloads that gained lose it.
- Neutral: the load generator's training spends most of its counts in the
  saturated throughput phase, which is a plausible reason the profile favours
  that path over the light-load one the latency phase times. Nothing here
  measures that reason.
- Neutral: each side was one binary. Link order alone moved two SPEC programs by
  4 and 2.6 per cent (Kalibera and Jones, ISMM 2013, section 8.2), and these
  runs don't separate it from the profile's effect.

## More Information

- ADR-0051 surveyed the compiler's remarks and adopted the opt-in presets.
- ADR-0054 describes the workflow, and `config/config.yaml` lists
  `conda-clang-pgo` under `pgo` so that adding it to `presets` repeats the
  comparison.
