---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0054. Measure on Friday through a Snakemake workflow, classifying before timing

## Context and Problem Statement

Measurements ran on the development machine, under WSL, where perf has no
hardware counters and ADR-0034 made cachegrind the only instrument. The owner
asked for measurement on UQ's Friday cluster, reached the way the Thesis
workflow reaches it, with no version typed by hand. Which mechanism runs the
measurements there, and what does a run control so that its numbers mean
something?

## Decision Drivers

- The Thesis repository already runs on Friday through the Slurm executor plugin
  (its ADR-0099) and a toolchain pinned by snakedeploy (its ADR-0106).
- A run takes its settings from a versioned file, and the archived file
  reproduces the run (Ingo and Daly, DBTest 2020, sections 2.1 and 2.4).
- A remedy is measured only for the bottleneck it addresses.

## Considered Options

- A Snakemake workflow with a Friday workflow profile and a conda toolchain, as
  in Thesis.
- `sbatch` scripts written by hand for each measurement.
- Keep measuring under WSL with cachegrind alone.

## Decision Outcome

The proposal is the Snakemake workflow.

- `workflow/Snakefile` builds the `conda-clang-rel` preset, lists the profiler's
  workloads from the built binary, and runs perf's TopdownL1 metric group over
  each one. `config/config.yaml` holds the preset and the repetition count.
- `workflow/envs/toolchain.yaml` names clang, lld, CMake, Ninja, linux-perf and
  Valgrind without versions. `snakedeploy pin-conda-envs` solved it in a Friday
  compute job against glibc 2.34, and Snakemake deploys the pin file. The vcpkg
  ports stay at the `vcpkg-configuration.json` baseline, which the shared vcpkg
  clone on Friday contains.
- `workflow/profiles/friday/profile.yaml` submits every job to one CPU model,
  the Gold 6226R nodes that Thesis times on, with one thread a core. A measured
  process gets its node to itself.

The run design follows the literature read for it.

- Top-down classification comes first (Yasin, ISPASS 2014, sections 3 and 3.1).
  Alignment, post-link layout and vectorisation are measured only once the
  category they address is flagged. The engine's text is about the size of a
  level-one instruction cache, and function placement gained through instruction
  cache and TLB misses in binaries of 70 megabytes and more (Ottoni and Maher,
  CGO 2017, section 2).
- Each measured process starts under `setarch -R` and `env -i`. Address
  randomisation and the size of the environment moved retired instruction counts
  by up to 1.07 per cent (Weaver and McKee, IISWC 2008, section 4.2.1).
- perf counts include interrupts and page faults (Weaver, Terpstra and Moore,
  ISPASS 2013, section III.A.1). A perf number is reported with an interval, and
  exact instruction comparisons stay with cachegrind.
- The initial experiment repeats each process 30 times, and its variances set
  the counts of later experiments (Kalibera and Jones, ISMM 2013, section 9.1).
  A comparison of layout-changing flags repeats at the binary level too, since
  link order alone moved two SPEC programs by 4 and 2.6 per cent (section 8.2).

### Consequences

- Positive: a tool writes every toolchain version, and the Thesis and Orderbooks
  workflows submit to Friday through the same plugin.
- Positive: perf's hardware counters become available through the pinned
  linux-perf, which WSL could not provide.
- Negative: the cluster keys of the workflow profile repeat the Thesis
  profile's. A global Friday profile would hold them once, and that awaits the
  owner's decision.
- Negative: the conda compiler is clang 23, so Friday numbers compare with each
  other and not with the clang 20 builds on the development machine.
- Neutral: the load generator's latency phase is closed-loop. Treadmill
  (ISCA 2016) and Tales of the tail (SoCC 2014, section 3.3) measure with
  open-loop Poisson arrivals, and a latency run on Friday waits for that mode.
- Neutral: COZ (SOSP 2015) would attribute the pipeline's throughput to its
  stages. conda-forge doesn't package it, and installing it is the owner's
  decision.

## More Information

- Thesis ADR-0099 and ADR-0106 describe the same arrangement for the Thesis
  workflow.
- ADR-0034 made cachegrind the instrument of record under WSL.
- `docs/dev/literature-review.md` cites the section behind each rule above.
