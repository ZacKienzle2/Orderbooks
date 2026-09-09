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

`bench/bench_*_tail.cpp` use [nanobench](https://nanobench.ankerl.com/) for p50
/ p99 / p99.9.

`bench/bench_engine_latency.cpp` times each `engine::on_submit` with the x86
time-stamp counter, records the per-operation samples into the in-process HDR
histogram (`lob::latency_histogram`, ADR-0024), and reports `p50`, `p99`,
`p99.9`, and `max` as benchmark counters. The unit is reference cycles, so
divide by the host's nominal frequency for nanoseconds.

## perf counters (Linux)

```bash
just perfstat
```

Output: `artifacts/perf/perf.txt`, written by `perf stat -o`. Events:
`cycles, instructions, branches, branch-misses, L1-dcache-load(-misses), LLC-load(-misses), dTLB-load-misses, iTLB-load-misses`.

## Regression tracking

CI runs
[github-action-benchmark](https://github.com/benchmark-action/github-action-benchmark)
over the run's `artifacts/bench.json`. The action reads Google Benchmark's own
JSON, keeps the history in a file that `actions/cache` carries between runs, and
writes the comparison with the previous run on `main` to the job summary. It
does not fail the job: consecutive runs land on different shared runners, and
the first comparison showed ratios of 1.2 to 2.9 on unchanged code, which is the
hardware rather than the engine; a relative gate needs a pinned host. A run on
`main` writes the history; a pull request is compared against it without moving
it. There is no baseline file to refresh.

## Production-quality runs

- Isolated cores (`isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3`).
- `cpupower frequency-set -g performance`.
- Turbo Boost off.
- `setarch -R` for ASLR-off comparisons.
- Hugepages: `echo 1024 | sudo tee /proc/sys/vm/nr_hugepages`.
- Pin: `taskset -c 2 ./lob_bench`.

CI numbers are for regression detection, not absolute claims.
