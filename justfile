# Task runner recipes. Each recipe is one packaged tool's own invocation over
# the CMake presets and targets the repository already defines; what the hooks
# or CMake do is not repeated here. `just --list` prints them.

preset := env("LOB_PRESET", "linux-clang-rel")

# Format C++ and CMake sources through the hooks that gate a commit.
format:
    prek run clang-format cmake-format --all-files

# clang-tidy over the preset's compilation database, with LLVM's own runner.
lint preset=preset:
    run-clang-tidy -p build/{{ preset }}

# perf stat counters over the benchmark binary under its fixed-seed workload.
perfstat preset=preset:
    mkdir -p artifacts/perf
    perf stat -o artifacts/perf/perf.txt -e cycles,instructions,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses,dTLB-load-misses,iTLB-load-misses -- build/{{ preset }}/bench/lob_bench --benchmark_repetitions=3 --benchmark_report_aggregates_only=true

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

# Build the libFuzzer harnesses and replay each corpus once under the
# sanitizers the preset carries.
fuzzers:
    cmake --workflow --preset linux-clang-fuzz

# Fuzz one harness for a budget, keeping its corpus between runs so later runs
# start from the coverage earlier ones found.
fuzz target="fix_framed" seconds="60": fuzzers
    build/linux-clang-fuzz/fuzz/lob_fuzz_{{ target }} artifacts/fuzz/corpus/{{ target }} -max_total_time={{ seconds }} -print_final_stats=1

# Fuzz every harness at once, each for the budget, as ctest runs the fuzzing
# tests that LOB_FUZZ_SECONDS registers.
fuzz-all seconds="60":
    cmake --preset linux-clang-fuzz -DLOB_FUZZ_SECONDS={{ seconds }}
    cmake --build --preset linux-clang-fuzz
    ctest --preset linux-clang-fuzz

# Train a profile. The workflow builds instrumented, runs every workload the
# profiler lists as a test, and merges the counts into artifacts/pgo. The
# workloads are the training set, so a hot path that no workload reaches gets
# no profile.
pgo-train:
    cmake --workflow --preset linux-clang-pgo-generate

# Release build that reads the trained profile.
pgo-build:
    cmake --workflow --preset linux-clang-pgo

# Build instrumented and run a coverage preset's tests, which merge their
# profiles: the test suite (linux-clang-coverage) or the fuzz corpora
# (linux-clang-fuzz-coverage). A corpus grown over minutes covers the parser
# further than the suite does, so the second is the honest figure for the code
# behind the harnesses.
coverage preset="linux-clang-coverage":
    cmake --workflow --preset {{ preset }}

# Region, line and branch coverage of this repository's sources, from the
# arguments the build writes for its instrumented binaries.
coverage-report preset="linux-clang-coverage": (coverage preset)
    llvm-cov report @build/{{ preset }}/coverage.rsp

# cppcheck, which needs no compilation database. Clang's path-sensitive
# analyser is not run here: .clang-tidy already selects the clang-analyzer
# checks, so `just lint` runs it against the preset's database, which resolves
# the dependencies' headers where a standalone invocation does not.
static:
    cppcheck --enable=warning,performance,portability,style --inline-suppr --std=c++20 --language=c++ --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=unmatchedSuppression --error-exitcode=1 -q -I include -I . include/lob apps

# API documentation from the headers. Graphs are drawn when graphviz is
# installed and skipped when it is not, so the build works either way.
docs:
    mkdir -p artifacts/doxygen
    DOXYGEN_HAVE_DOT=$(command -v dot >/dev/null && echo YES || echo NO) doxygen Doxyfile
    @echo "artifacts/doxygen/html/index.html"
