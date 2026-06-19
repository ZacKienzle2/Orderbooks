# Benchmarks

## Run microbenches

```bash
cmake --preset linux-clang-rel
cmake --build --preset linux-clang-rel --target lob_bench --parallel

./build/linux-clang-rel/bench/lob_bench \
  --benchmark_min_time=1.0s \
  --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true \
  --benchmark_format=json \
  --benchmark_out=artifacts/bench.json \
  --benchmark_out_format=json
```

## Tail latencies

`bench/bench_*_tail.cpp` use [nanobench](https://nanobench.ankerl.com/) for p50 / p99 / p99.9.

`bench/bench_engine_latency.cpp` times each `engine::on_submit` with the x86
time-stamp counter, records the per-operation samples into the in-process HDR
histogram (`lob::latency_histogram`, ADR-0024), and reports `p50`, `p99`,
`p99.9`, and `max` as benchmark counters. The unit is reference cycles, so
divide by the host's nominal frequency for nanoseconds.

## perf counters (Linux)

```bash
./scripts/perfstat.sh
```

Output: `artifacts/perf/perf-<utc>.txt`. Events: `cycles, instructions, branches, branch-misses, L1-dcache-load(-misses), LLC-load(-misses), dTLB-load-misses, iTLB-load-misses`.

## Latency ceiling gate

CI also runs an absolute, baseline-free gate over the latency benchmark.
`scripts/check_latency_ceiling.py` reads the run's own `artifacts/bench.json`,
takes the median aggregate of `bench_submit_latency`, and fails the build when a
percentile exceeds its ceiling. Unlike the relative gate it needs no baseline,
so it stays active from the first run and catches a gross algorithmic regression
(an O(1) path turned linear) that a drifting baseline would absorb.

Ceilings are reference cycles, set in `bench.yml` and overridable per run.

```bash
LATENCY_P50_CEILING=600 LATENCY_P999_CEILING=8000 \
  python3 scripts/check_latency_ceiling.py artifacts/bench.json
```

Exit codes are the contract. 0 within ceiling, 1 on a breach, 2 on a missing
file, absent benchmark, or absent counter.

## Regression gate

CI runs `google/benchmark`'s `compare.py` against `bench/baseline.json`.

Refresh the baseline on a quiet, pinned host:

```bash
cp artifacts/bench.json bench/baseline.json
git checkout -b perf/refresh-baseline
git add bench/baseline.json
git commit -m "perf(bench): refresh baseline" \
           -m "Captured on $(uname -srvmo) with $(clang++ --version | head -n1)."
```

## Production-quality runs

- Isolated cores (`isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3`).
- `cpupower frequency-set -g performance`.
- Turbo Boost off.
- `setarch -R` for ASLR-off comparisons.
- Hugepages: `echo 1024 | sudo tee /proc/sys/vm/nr_hugepages`.
- Pin: `taskset -c 2 ./lob_bench`.

CI numbers are for regression detection, not absolute claims.
