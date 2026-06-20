# Profiling

The engine is profiled with synthetic order flow, so the numbers reproduce on
any host with no market data. Two pieces do the work. `apps/profile`
(`lob_profile`) generates the flow and drives one engine on one thread.
`scripts/profile.sh` runs that profiler under analysis plugins.

## Driver

`lob_profile` runs one selectable workload and prints reference cycles per
operation over a timed region. One engine, one thread, deterministic dispatch,
so the counters belong to the engine rather than to thread scheduling or to a
random op-dispatch. Pre-population runs outside the timed region.

```bash
cmake --build --preset linux-clang-rel --target lob_profile --parallel
./build/linux-clang-rel/apps/profile/lob_profile --list
./build/linux-clang-rel/apps/profile/lob_profile --workload deep --ops 20000000 --depth 40000
```

Workloads are `deep` (the depth-maintaining replace and modify mix, the
realistic resting path), the isolated `submit`, `cancel`, `modifyp`, `modifyq`,
`cross`, and `sweep` (a tall single-price FIFO drained per aggressor, reported
as cycles per fill).

## Plugins

```bash
scripts/profile.sh                 # all plugins
scripts/profile.sh --plugin perf   # one plugin
scripts/profile.sh --ops 40000000 --depth 80000
```

- `perf` runs `perf stat` over each workload and tabulates cycles per op, IPC,
  branch-miss rate, and L1 miss rate. This is the micro-optimisation signal. A
  low IPC with a high miss rate points to a memory-bound path; a high branch
  miss rate to a data-dependent branch worth hoisting or making branchless.
- `sanitize` runs an ASAN and UBSAN soak over the deep mix. This is the
  implementation-error signal, catching a memory or undefined-behaviour fault
  that an optimisation can introduce.
- `record` runs `perf record` and prints the top source lines by time over the
  deep mix. This is the missed-opportunity signal, naming the lines to attack.

The `perf` and `record` plugins need a Linux host with `perf` and a PMU. A
virtualised host often exposes counting (`perf stat`) but not sampling
(`perf record`); the `record` plugin then reports that and is skipped.

## Reading the result

The cheap operations (`cancel`, `cross`, `modifyq`) run near the compute ceiling
at five or more instructions per cycle and want no further work. The cost sits
in `submit` and `modifyp`, both memory-latency-bound on the random arena, index,
and level accesses an order book makes by nature. The structures are already
cache-friendly (dense ladder, slab arena, open-addressed index), so the
remaining levers are host-level. The huge-page arena (ADR-0023) and NUMA
first-touch (ADR-0016) cut the data-TLB and cross-node misses that show up only
on production hardware with reserved huge pages and isolated cores, not on a
shared laptop or a CI runner.

## Discipline

A change found here ships only after an A/B on a quiet host, the same standard
the microbenchmarks and the latency gate hold. Several candidates have been
measured and rejected when the number did not hold up (ADR-0027 for the match
prefetch). Profile, change one thing, measure, keep it only if it wins.
