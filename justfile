# Task runner recipes. Each recipe is one packaged tool's own invocation over
# the CMake presets and targets the repository already defines; what the hooks
# or CMake do is not repeated here. `just --list` prints them.

preset := env("LOB_PRESET", "linux-clang-rel")

# Format C++ and CMake sources through the hooks that gate a commit.
format:
    pre-commit run clang-format cmake-format --all-files

# clang-tidy over the preset's compilation database, with LLVM's own runner.
lint preset=preset:
    run-clang-tidy -p build/{{ preset }}

# perf stat counters over the benchmark binary under its fixed-seed workload.
perfstat preset=preset:
    mkdir -p artifacts/perf
    perf stat -o artifacts/perf/perf.txt -e cycles,instructions,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses,dTLB-load-misses,iTLB-load-misses -- build/{{ preset }}/bench/lob_bench --benchmark_min_time=1.0s --benchmark_repetitions=3 --benchmark_report_aggregates_only=true

# Build the synthetic-flow profiler for a preset.
profiler preset=preset:
    cmake --build --preset {{ preset }} --target lob_profile --parallel

# perf stat over one workload: cycles per op, IPC, branch and L1 miss rates.
profile-perf workload="deep" ops="20000000" depth="40000" preset=preset: (profiler preset)
    perf stat -e instructions,cycles,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,dTLB-load-misses -- build/{{ preset }}/apps/profile/lob_profile --workload {{ workload }} --ops {{ ops }} --depth {{ depth }}

# perf stat over every workload.
profile-perf-all ops="20000000" depth="40000" preset=preset: (profile-perf "deep" ops depth preset) (profile-perf "submit" ops depth preset) (profile-perf "cancel" ops depth preset) (profile-perf "modifyp" ops depth preset) (profile-perf "modifyq" ops depth preset) (profile-perf "cross" ops depth preset) (profile-perf "sweep" ops depth preset)

# ASAN and UBSAN soak over the deep mix, built from the sanitizer preset.
profile-sanitize ops="1000000" depth="40000": (profiler "linux-clang-asan")
    build/linux-clang-asan/apps/profile/lob_profile --workload deep --ops {{ ops }} --depth {{ depth }}

# perf record source-line hot spots over the deep mix.
profile-record ops="20000000" depth="40000" preset=preset: (profiler preset)
    mkdir -p artifacts/perf
    perf record -e task-clock -F 4000 -o artifacts/perf/profile.data -- build/{{ preset }}/apps/profile/lob_profile --workload deep --ops {{ ops }} --depth {{ depth }}
    perf report -i artifacts/perf/profile.data --stdio -n --sort=srcline

# RelWithDebInfo carries the -g for source attribution and the x86-64-v3 target
# the simulator decodes; the simulator runs about a hundred times slower than
# native, hence the op count.
# Cachegrind D1 miss attribution for one workload.
profile-cachegrind workload="deep" ops="200000" depth="40000": (profiler "linux-clang-relwithdebinfo")
    mkdir -p artifacts/cachegrind
    valgrind --tool=cachegrind --cache-sim=yes --cachegrind-out-file=artifacts/cachegrind/cachegrind.out.{{ workload }} -- build/linux-clang-relwithdebinfo/apps/profile/lob_profile --workload {{ workload }} --ops {{ ops }} --depth {{ depth }}
    cg_annotate artifacts/cachegrind/cachegrind.out.{{ workload }}

# Cachegrind over the memory-bound workloads.
profile-cachegrind-all ops="200000" depth="40000": (profile-cachegrind "deep" ops depth) (profile-cachegrind "submit" ops depth) (profile-cachegrind "modifyp" ops depth)
