# Build

## Prerequisites

CMake 3.28+, a C++20 compiler (Clang 17+, GCC 13+, Apple Clang 15+), Ninja, vcpkg (with `VCPKG_ROOT` exported), `uv`.

## First-time setup

```bash
git clone https://github.com/ZacKienzle2/Orderbooks
cd Orderbooks
export VCPKG_ROOT="$HOME/code/vcpkg"
"$VCPKG_ROOT/bootstrap-vcpkg.sh"

uv sync --frozen
uv run pre-commit install --install-hooks
```

## Presets

| Preset | Notes |
| --- | --- |
| `linux-clang-rel` | Release, LTO on. |
| `linux-clang-relwithdebinfo` | Symbols + optimisation. |
| `linux-clang-debug` | Day-to-day Linux. |
| `linux-gcc-rel`, `linux-gcc-debug` | Toolchain coverage. |
| `linux-clang-asan` | ASan + UBSan. |
| `linux-clang-tsan` | TSan. |
| `linux-clang-msan` | MSan (needs instrumented libc++). |
| `linux-clang-fuzz` | libFuzzer + ASan + UBSan. |
| `macos-clang-dev`, `macos-clang-rel`, `macos-clang-debug` | macOS. |

## Configure, build, test

```bash
cmake --preset linux-clang-rel
cmake --build --preset linux-clang-rel --parallel
ctest --preset linux-clang-rel --output-on-failure
```

## Targets

| Target | Description |
| --- | --- |
| `lob_core` | Engine + book + arena + bitmap + id index + SPSC ring. |
| `lob_tests` | Catch2 + RapidCheck. |
| `lob_bench` | Google Benchmark. |

## Options

| Option | Default | Effect |
| --- | --- | --- |
| `LOB_ENABLE_LTO` | ON in `linux-clang-rel` | Link-time optimisation. |
| `LOB_ENABLE_NATIVE` | OFF | `-march=native` instead of `x86-64-v3`. |
| `LOB_ENABLE_HARDENING` | ON | Stack protector, CET, FORTIFY_SOURCE. |
| `LOB_SANITIZER` | "" | Comma list: `address,undefined`, `thread`, `memory`, `fuzzer,address`. |
| `LOB_BUILD_TESTS` | ON | Build `lob_tests`. |
| `LOB_BUILD_BENCH` | ON | Build `lob_bench`. |
| `LOB_BUILD_FUZZ` | OFF | Build libFuzzer harnesses. |

## Formatting and linting

```bash
./scripts/format.sh
./scripts/lint.sh
uv run pre-commit run --all-files
```
