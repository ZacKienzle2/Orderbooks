#!/usr/bin/env bash
# Wrap perf stat around the benchmark binary to capture microarchitectural counters
# under a fixed-seed workload. Linux only.
set -euo pipefail

if [ "$(uname -s)" != "Linux" ]; then
  echo "perfstat.sh requires Linux."
  exit 2
fi

if ! command -v perf >/dev/null 2>&1; then
  echo "perf is not installed. apt: linux-tools-common linux-tools-generic."
  exit 2
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "${repo_root}"

preset="${LOB_PRESET:-linux-clang-rel}"
binary="build/${preset}/bench/lob_bench"

if [ ! -x "${binary}" ]; then
  echo "Benchmark binary missing: ${binary}"
  echo "Build: cmake --build --preset ${preset} --target lob_bench"
  exit 1
fi

mkdir -p artifacts/perf

events="cycles,instructions,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses,dTLB-load-misses,iTLB-load-misses"

out="artifacts/perf/perf-$(date -u +%Y%m%dT%H%M%SZ).txt"

perf stat -e "${events}" -- "${binary}" \
  --benchmark_min_time=1.0s \
  --benchmark_repetitions=3 \
  --benchmark_report_aggregates_only=true \
  2> "${out}"

cat "${out}"
echo
echo "perf counters written to ${out}"
