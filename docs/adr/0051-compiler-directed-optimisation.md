---
status: "Accepted"
date: "2026-09-23"
deciders: ["Zac Kienzle"]
---

# 0051. Compiler-directed optimisation: remarks surveyed, profile adopted

## Context and Problem Statement

The engine's hot paths have been measured down to their cache-miss floor
(ADR-0034) and their instruction counts, and the remaining candidates were all
source changes someone would have to think of. Two tools report what to do
without being asked. Clang's optimisation records say which transformations it
attempted and could not apply, and where. Profile-guided optimisation lets the
program's own behaviour decide inlining and block layout rather than the
compiler's static guesses.

Neither had been used here.

## Decision Drivers

- The tool should locate the inefficiency, rather than a person guessing.
- A change ships only with a measurement, and the host is not quiet, so the
  measurement must be deterministic.
- An optimisation that depends on a stale input is worse than none.

## Considered Options

- Read the optimisation records and act on the largest category.
- Profile-guided optimisation, trained on the engine's own workloads.
- Link-time optimisation, already scaffolded as `LOB_ENABLE_LTO`.
- BOLT, which rearranges a linked binary from a profile.

## Decision Outcome

Chosen: **adopt profile-guided optimisation as an opt-in build mode**, and
**reject the change the optimisation records suggested**, having measured both.

### The records, and what they were worth

Compiling the three applications with `-fsave-optimization-record` and
summarising the remarks that fall inside this repository's headers gives one
dominant category: 5,718 `LoadClobbered` remarks, a load the compiler had to
repeat because a store might have aliased it. The largest sites were the price
ladder in `book.hpp` and the engine's own state reads around the publish path.

That is a real aliasing report, and the remedy the microarchitecture literature
prescribes is to restrict-qualify the pointer, which is sound here because the
ladder is its own heap allocation and a store into a level cannot reach the
bitmap, the cached best, or the engine state. Applying `__restrict` to the
ladder pointer at the two functions that store through it, counted with
cachegrind over four workloads at 400,000 operations each:

| Workload | Instructions, before |       after |
| -------- | -------------------: | ----------: |
| deep     |          131,868,710 | 131,868,710 |
| submit   |          151,536,284 | 151,536,284 |
| cancel   |          111,921,655 | 111,921,519 |
| cross    |           81,899,113 |  81,899,140 |

Identical, to within a hundred instructions in a hundred million, with every
cache-miss count unchanged. The change was reverted.

The lesson is about the tool rather than about aliasing. A remark is emitted per
inlined copy, so the counts measure how often a line was inlined as much as what
it costs, and a load that is clobbered but hits L1 and sits off the critical
path costs nothing to repeat. The records are a map of where the compiler gave
up; they are not a ranking of what to fix.

### The profile, and what it was worth

Training on every workload `lob_profile` defines and rebuilding with the merged
profile, counted the same way:

| Workload | Instructions, release | with profile | Change |
| -------- | --------------------: | -----------: | -----: |
| deep     |           131,868,644 |  128,591,401 |  -2.5% |
| submit   |           151,536,473 |  150,742,218 |  -0.5% |
| cancel   |           111,921,690 |  109,090,600 |  -2.5% |
| cross    |            81,899,090 |   80,717,526 |  -1.4% |

Cache misses are unchanged in every workload, which is what a change to inlining
and block layout should look like: the same data touched in the same order,
fewer instructions to do it.

`LOB_PGO` takes `off`, `generate` or `use`, `just pgo-train` produces the
profile from the workloads, and `just pgo-build` consumes it. Configuring `use`
without a profile file is a configure-time error rather than a silent build
without one.

Two diagnostics need a position under `-Werror`. The training set is the
engine's workloads, so the gateway, the replay tool and the generated version
file are legitimately unprofiled, and `-Wprofile-instr-unprofiled` is off. A
profile that disagrees with a function it does know means the profile has aged
past the code, which is worth seeing, so `-Wprofile-instr-out-of-date` stays a
warning and is only demoted from an error.

### The two that do not apply

Link-time optimisation has nothing to do here. The library is header-only and
each application is a single translation unit, so there is no cross-unit
inlining left for the linker to find.

BOLT rearranges a binary to reduce instruction-cache and iTLB pressure. This
workload has none: cachegrind counts about 2,400 first-level instruction-cache
misses against 130 million instructions, a rate of 0.002 per cent. There is
nothing for it to recover, and it is not worth the build step.

### Consequences

- Positive: between 0.5 and 2.5 per cent of instructions, from a build mode
  rather than from a source change.
- Negative: the profile is a build input that ages. It is off by default, and
  regenerating it is a documented step rather than an automatic one.
- Negative: the training set defines what gets optimised. A hot path no workload
  reaches gets no profile, which ties the value of this to the profiler's
  workload coverage.
- The optimisation records stay useful as a map for a future change, but nothing
  should be adopted from them without the same measurement.

## More Information

- Related: ADR-0027 and ADR-0041 (the same measure-then-reject pattern),
  ADR-0034 (the cache floor these numbers sit on).
- Measurement: cachegrind over `lob_profile`, whose counts are exact and so need
  no quiet host.

- Chen, D., Li, D. X., & Moseley, T. (2016). AutoFDO: automatic
  feedback-directed optimization for warehouse-scale applications. _CGO_.
  <https://doi.org/10.1145/2854038.2854044>
- Panchenko, M., Auler, R., Nell, B., & Ottoni, G. (2019). BOLT: a practical
  binary optimizer for data centers and beyond. _CGO_.
  <https://doi.org/10.1109/CGO.2019.8661201>
- Nethercote, N., & Seward, J. (2007). Valgrind: a framework for heavyweight
  dynamic binary instrumentation. _PLDI_.
  <https://doi.org/10.1145/1250734.1250746>

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
