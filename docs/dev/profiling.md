# Profiling

The engine is profiled with synthetic order flow, whose numbers reproduce on any
host without market data. `apps/profile` (`lob_profile`) generates that flow and
drives one engine on one thread, and the `profile-*` recipes in the `justfile`
run the profiler under the analysis tools, each recipe one tool's own invocation
over a binary that `conan build` produced.

## Driver

`lob_profile` runs one selectable workload and prints reference cycles per
operation over a timed region. It drives one engine from one thread with
deterministic dispatch. The counters then measure the engine rather than thread
scheduling or a random op-dispatch. Pre-population runs outside the timed
region.

```bash
just release
./build/lto_on/Release/apps/profile/lob_profile --list
./build/lto_on/Release/apps/profile/lob_profile --workload deep --ops 20000000 --depth 40000
```

Workloads are `deep` (the depth-maintaining replace and modify mix, the
realistic resting path), the isolated `submit`, `cancel`, `modifyp`, `modifyq`,
`cross`, and `sweep` (a tall single-price FIFO drained per aggressor, reported
as cycles per fill).

## Plugins

```bash
just --list                                  # every recipe with its parameters
just profile-perf-all                        # perf stat over every workload
just profile-perf deep 40000000 80000        # one workload, ops, depth
just profile-sanitize                        # ASAN and UBSAN soak, in the sanitizer build
just profile-record                          # perf record, top source lines
just profile-cachegrind-all                  # cachegrind over deep, submit, modifyp
```

- `profile-perf` runs `perf stat` over a workload; the profiler prints cycles
  per op and perf prints IPC, branch-miss rate and L1 miss rate. This is the
  micro-optimisation signal. A low IPC with a high miss rate points to a
  memory-bound path; a high branch miss rate to a data-dependent branch to hoist
  or make branchless.
- `sanitize` runs an ASAN and UBSAN soak over the deep mix. This is the
  coding-error signal, catching a memory or undefined-behaviour fault that an
  optimisation can introduce.
- `record` runs `perf record` and prints the top source lines by time over the
  deep mix. This is the missed-opportunity signal, naming the lines to attack.
- `cachegrind` runs the memory-bound workloads (`deep`, `submit`, `modifyp`)
  under valgrind's cache simulator and prints per-function D1 miss attribution.
  This is the where-do-the-misses-live signal and the methodology behind
  ADR-0034's miss breakdown. It doesn't need a PMU. It is exact on shared
  runners and virtual machines where `perf` cannot count; the trade is a ~100x
  slowdown, which the plugin absorbs by scaling ops down 100x.

The `perf` and `record` plugins need a Linux host with `perf` and a PMU. A
virtualised host often exposes counting (`perf stat`) but not sampling
(`perf record`). The `record` plugin then reports that and is skipped. The
`cachegrind` plugin needs only valgrind, and the `Cachegrind` workflow
(`.github/workflows/cachegrind.yml`, manual dispatch) runs it on a CI runner and
uploads the report plus raw profiles, so miss attribution is available
push-button from any development host:

```bash
gh workflow run cachegrind.yml
gh run download --name cachegrind-report
```

## Reading the result

The cheap operations (`cancel`, `cross`, `modifyq`) run near the compute ceiling
at five or more instructions per cycle and want no further work. The cost is in
`submit` and `modifyp`, both memory-latency-bound on the random arena, index,
and level accesses an order book makes by nature. The structures are already
cache-friendly (dense ladder, slab arena, open-addressed index), so the
remaining levers are host-level. The huge-page arena (ADR-0023) and NUMA
first-touch (ADR-0016) cut the data-TLB and cross-node misses that show up only
on production hardware with reserved huge pages and isolated cores, not on a
shared laptop or a CI runner.

## Discipline

A change found here merges only after an A/B on a quiet host, the same standard
the microbenchmarks and the latency gate apply. Several candidates have been
measured and rejected when the number did not hold up (ADR-0027 for the match
prefetch). Profile, change one thing, measure, keep it only if it wins.
