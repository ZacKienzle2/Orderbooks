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

# Build the libFuzzer harnesses. The preset carries the sanitizer set.
fuzzers:
    cmake --build --preset linux-clang-fuzz --target lob_fuzz_fix_raw lob_fuzz_fix_framed lob_fuzz_snapshot_restore lob_fuzz_gateway_wire --parallel

# Fuzz one harness for a budget, keeping its corpus between runs so later runs
# start from the coverage earlier ones found.
fuzz target="fix_framed" seconds="60": fuzzers
    mkdir -p artifacts/fuzz/corpus/{{ target }}
    build/linux-clang-fuzz/fuzz/lob_fuzz_{{ target }} artifacts/fuzz/corpus/{{ target }} -max_total_time={{ seconds }} -print_final_stats=1

# Fuzz every harness in turn.
fuzz-all seconds="60": (fuzz "fix_raw" seconds) (fuzz "fix_framed" seconds) (fuzz "snapshot_restore" seconds) (fuzz "gateway_wire" seconds)

# Train a profile: build instrumented, run every workload the profiler defines,
# and merge the raw counts. The workloads are the training set, so a hot path
# that no workload reaches gets no profile.
pgo-train ops="2000000" depth="40000":
    cmake -S . -B build/pgo-train -G Ninja -DCMAKE_BUILD_TYPE=Release -DLOB_PGO=generate
    cmake --build build/pgo-train --target lob_profile --parallel
    mkdir -p artifacts/pgo/raw
    for w in deep submit cancel modifyp modifyq cross sweep; do         LLVM_PROFILE_FILE="artifacts/pgo/raw/$w.profraw"             build/pgo-train/apps/profile/lob_profile --workload "$w" --ops {{ ops }} --depth {{ depth }} >/dev/null;     done
    llvm-profdata merge -output=artifacts/pgo/train.profdata artifacts/pgo/raw/*.profraw

# Release build that reads the trained profile.
pgo-build:
    cmake -S . -B build/pgo -G Ninja -DCMAKE_BUILD_TYPE=Release -DLOB_PGO=use
    cmake --build build/pgo --parallel

# Region, line and branch coverage of the library under the test suite.
coverage:
    cmake -S . -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLOB_COVERAGE=ON
    cmake --build build/coverage --target lob_tests --parallel
    mkdir -p artifacts/coverage
    LLVM_PROFILE_FILE=artifacts/coverage/tests.profraw build/coverage/tests/lob_tests
    llvm-profdata merge -sparse artifacts/coverage/tests.profraw -o artifacts/coverage/tests.profdata
    llvm-cov report build/coverage/tests/lob_tests -instr-profile=artifacts/coverage/tests.profdata -ignore-filename-regex='(vcpkg|catch2|rapidcheck|/usr/|/tests/)'

# What the fuzz corpora reach, replayed under the same instrumentation. A
# corpus grown over minutes covers the parser further than the suite does, so
# this is the honest figure for the code behind the harnesses.
coverage-fuzz:
    cmake -S . -B build/coverage-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLOB_COVERAGE=ON -DLOB_BUILD_FUZZ=ON -DLOB_BUILD_TESTS=OFF -DLOB_BUILD_BENCH=OFF -DLOB_SANITIZER=""
    cmake --build build/coverage-fuzz --parallel
    mkdir -p artifacts/coverage
    for t in fix_raw fix_framed snapshot_restore gateway_wire; do         LLVM_PROFILE_FILE="artifacts/coverage/$t.profraw"             build/coverage-fuzz/fuzz/lob_fuzz_$t "artifacts/fuzz/corpus/$t" -runs=0 >/dev/null 2>&1;     done
    llvm-profdata merge -sparse artifacts/coverage/*.profraw -o artifacts/coverage/fuzz.profdata
    llvm-cov report build/coverage-fuzz/fuzz/lob_fuzz_fix_raw -object build/coverage-fuzz/fuzz/lob_fuzz_fix_framed -object build/coverage-fuzz/fuzz/lob_fuzz_snapshot_restore -object build/coverage-fuzz/fuzz/lob_fuzz_gateway_wire -instr-profile=artifacts/coverage/fuzz.profdata -ignore-filename-regex='(vcpkg|/usr/|fuzz_)'
