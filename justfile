# Task runner recipes. Each recipe is one packaged tool's own invocation over
# the Conan configurations and targets the repository already defines; what the
# hooks, Conan or CMake do is not repeated here. `just --list` prints them.
#
# `conan build` installs the dependencies, writes the toolchain and presets,
# then configures, builds and runs the tests. cmake_layout names each build
# folder after the options given and the build type, so a binary lives at
# build/<options>/<build type>/.

# The sources are C++20, and a dependency without a ConanCenter binary for the
# profile is built from source.
conan := "conan build . -s:a compiler.cppstd=20 --build=missing"

# CI's release configuration, which the perf recipes measure.
release_options := '-o "&:lto=ON"'
release_dir := "build/lto_on/Release"

# The sanitizers libFuzzer runs under, with the harnesses built.
fuzz_options := '-s build_type=Debug -o "&:sanitizer=address,undefined" -o "&:fuzz=ON"'
fuzz_dir := "build/sanitizer_address,undefined-fuzz_on/Debug"

# Format C++ and CMake sources through the hooks that gate a commit.
format:
    prek run clang-format cmake-format --all-files

# Build and test a configuration, given as Conan settings and options, such as
# `just build -s build_type=Debug -o "&:sanitizer=thread"`.
build *args:
    {{ conan }} {{ args }}

# Build and test the release configuration.
release: (build release_options)

# clang-tidy over a build's compilation database, with LLVM's own runner.
lint dir=release_dir:
    run-clang-tidy -p {{ dir }}

# perf stat counters over the benchmark binary under its fixed-seed workload.
perfstat: release
    mkdir -p artifacts/perf
    perf stat -o artifacts/perf/perf.txt -e cycles,instructions,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses,dTLB-load-misses,iTLB-load-misses -- {{ release_dir }}/bench/lob_bench --benchmark_min_time=1.0s --benchmark_repetitions=3 --benchmark_report_aggregates_only=true

# perf stat over one workload: cycles per op, IPC, branch and L1 miss rates.
profile-perf workload="deep" ops="20000000" depth="40000": release
    perf stat -e instructions,cycles,branches,branch-misses,L1-dcache-loads,L1-dcache-load-misses,dTLB-load-misses -- {{ release_dir }}/apps/profile/lob_profile --workload {{ workload }} --ops {{ ops }} --depth {{ depth }}

# perf stat over every workload.
profile-perf-all ops="20000000" depth="40000": (profile-perf "deep" ops depth) (profile-perf "submit" ops depth) (profile-perf "cancel" ops depth) (profile-perf "modifyp" ops depth) (profile-perf "modifyq" ops depth) (profile-perf "cross" ops depth) (profile-perf "sweep" ops depth)

# ASAN and UBSAN soak over the deep mix, built with the sanitizers.
profile-sanitize ops="1000000" depth="40000": (build '-s build_type=Debug -o "&:sanitizer=address,undefined"')
    "build/sanitizer_address,undefined/Debug/apps/profile/lob_profile" --workload deep --ops {{ ops }} --depth {{ depth }}

# perf record source-line hot spots over the deep mix.
profile-record ops="20000000" depth="40000": release
    mkdir -p artifacts/perf
    perf record -e task-clock -F 4000 -o artifacts/perf/profile.data -- {{ release_dir }}/apps/profile/lob_profile --workload deep --ops {{ ops }} --depth {{ depth }}
    perf report -i artifacts/perf/profile.data --stdio -n --sort=srcline

# RelWithDebInfo carries the -g for source attribution and the x86-64-v3 target
# the simulator decodes; the simulator runs about a hundred times slower than
# native, hence the op count.
# Cachegrind D1 miss attribution for one workload.
profile-cachegrind workload="deep" ops="200000" depth="40000": (build "-s build_type=RelWithDebInfo")
    mkdir -p artifacts/cachegrind
    valgrind --tool=cachegrind --cache-sim=yes --cachegrind-out-file=artifacts/cachegrind/cachegrind.out.{{ workload }} -- build/RelWithDebInfo/apps/profile/lob_profile --workload {{ workload }} --ops {{ ops }} --depth {{ depth }}
    cg_annotate artifacts/cachegrind/cachegrind.out.{{ workload }}

# Cachegrind over the memory-bound workloads.
profile-cachegrind-all ops="200000" depth="40000": (profile-cachegrind "deep" ops depth) (profile-cachegrind "submit" ops depth) (profile-cachegrind "modifyp" ops depth)

# Build the libFuzzer harnesses and replay each corpus once under the
# sanitizers, with the unit tests.
fuzzers: (build fuzz_options)

# Fuzz one harness for a budget, keeping its corpus between runs so later runs
# start from the coverage earlier ones found.
fuzz target="fix_framed" seconds="60": fuzzers
    "{{ fuzz_dir }}/fuzz/lob_fuzz_{{ target }}" artifacts/fuzz/corpus/{{ target }} -max_total_time={{ seconds }} -print_final_stats=1

# Fuzz every harness at once, each for the budget, as ctest runs the fuzzing
# tests that LOB_FUZZ_SECONDS registers.
fuzz-all seconds="60":
    {{ conan }} {{ fuzz_options }} -c "&:tools.cmake.cmaketoolchain:extra_variables={'LOB_FUZZ_SECONDS': '{{ seconds }}'}"

# Train a profile. The build is instrumented, runs every workload the profiler
# lists as a test, and merges the counts into artifacts/pgo. The workloads are
# the training set, so the unit tests are left out, and a hot path that no
# workload reaches gets no profile.
pgo-train: (build '-o "&:tests=OFF" -o "&:pgo=generate"')

# Release build that reads the trained profile.
pgo-build: (build '-o "&:pgo=use"')

# Build instrumented and run the test suite, whose profiles the build merges.
coverage: (build '-s build_type=Debug -o "&:coverage=ON"')

# The same over the fuzz corpora alone. A corpus grown over minutes covers the
# parser further than the suite does, so this is the honest figure for the code
# behind the harnesses.
fuzz-coverage: (build '-s build_type=Debug -o "&:fuzz=ON" -o "&:tests=OFF" -o "&:coverage=ON"')

# Region, line and branch coverage of this repository's sources, from the
# arguments the build writes for its instrumented binaries.
coverage-report: coverage
    llvm-cov report @build/coverage_on/Debug/coverage.rsp

# The same for the fuzz corpora.
fuzz-coverage-report: fuzz-coverage
    llvm-cov report @build/fuzz_on-tests_off-coverage_on/Debug/coverage.rsp

# cppcheck, which needs no compilation database. Clang's path-sensitive
# analyser is not run here: .clang-tidy already selects the clang-analyzer
# checks, so `just lint` runs it against a build's database, which resolves
# the dependencies' headers where a standalone invocation does not.
static:
    cppcheck --enable=warning,performance,portability,style --inline-suppr --std=c++20 --language=c++ --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=unmatchedSuppression --error-exitcode=1 -q -I include -I . include/lob apps

# API documentation from the headers. Graphs are drawn when graphviz is
# installed and skipped when it is not, so the build works either way.
docs:
    mkdir -p artifacts/doxygen
    DOXYGEN_HAVE_DOT=$(command -v dot >/dev/null && echo YES || echo NO) doxygen Doxyfile
    @echo "artifacts/doxygen/html/index.html"
