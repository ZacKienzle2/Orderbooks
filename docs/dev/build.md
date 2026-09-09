# Build

## Prerequisites

CMake 3.28+, a C++20 compiler (Clang 17+, GCC 13+, Apple Clang 15+), Ninja,
vcpkg (with `VCPKG_ROOT` exported), `uv`, `just`. The `Brewfile` names them for
macOS; `brew bundle` installs it.

## First-time setup

```bash
git clone https://github.com/ZacKienzle2/Orderbooks
cd Orderbooks

brew bundle                          # the Brewfile: cmake, ninja, llvm, ccache, uv, pre-commit, just
uv sync --frozen
pre-commit install --install-hooks   # every formatter and linter, into pre-commit's cache

export VCPKG_ROOT="$HOME/code/vcpkg"
"$VCPKG_ROOT/bootstrap-vcpkg.sh"
```

Linux contributors: the same packages from `apt-get` or `dnf`, then the same
`uv` and `pre-commit` commands.

## Presets

| Preset                                                    | Notes                             |
| --------------------------------------------------------- | --------------------------------- |
| `linux-clang-rel`                                         | Release, LTO on.                  |
| `linux-clang-relwithdebinfo`                              | Symbols + optimisation.           |
| `linux-clang-debug`                                       | Day-to-day Linux.                 |
| `linux-gcc-rel`, `linux-gcc-debug`                        | Toolchain coverage.               |
| `linux-clang-asan`                                        | ASan + UBSan.                     |
| `linux-clang-tsan`                                        | TSan.                             |
| `linux-clang-msan`                                        | MSan (needs instrumented libc++). |
| `linux-clang-fuzz`                                        | libFuzzer + ASan + UBSan.         |
| `macos-clang-dev`, `macos-clang-rel`, `macos-clang-debug` | macOS.                            |

## Configure, build, test

```bash
cmake --preset linux-clang-rel
cmake --build --preset linux-clang-rel --parallel
ctest --preset linux-clang-rel --output-on-failure
```

## Targets

| Target      | Description                                            |
| ----------- | ------------------------------------------------------ |
| `lob_core`  | Engine + book + arena + bitmap + id index + SPSC ring. |
| `lob_tests` | Catch2 + RapidCheck.                                   |
| `lob_bench` | Google Benchmark.                                      |

## Options

| Option                 | Default                 | Effect                                                                 |
| ---------------------- | ----------------------- | ---------------------------------------------------------------------- |
| `LOB_ENABLE_LTO`       | ON in `linux-clang-rel` | Link-time optimisation.                                                |
| `LOB_ENABLE_NATIVE`    | OFF                     | `-march=native` instead of `x86-64-v3`.                                |
| `LOB_ENABLE_HARDENING` | ON                      | Stack protector, CET, FORTIFY_SOURCE.                                  |
| `LOB_SANITIZER`        | ""                      | Comma list: `address,undefined`, `thread`, `memory`, `fuzzer,address`. |
| `LOB_BUILD_TESTS`      | ON                      | Build `lob_tests`.                                                     |
| `LOB_BUILD_BENCH`      | ON                      | Build `lob_bench`.                                                     |
| `LOB_BUILD_FUZZ`       | OFF                     | Build libFuzzer harnesses.                                             |

## Formatting and linting

```bash
just format            # clang-format and cmake-format, through the hooks
just lint              # run-clang-tidy over build/<preset>/compile_commands.json
pre-commit run --all-files
```

`just lint` reads `LOB_PRESET` for the preset, `linux-clang-rel` by default, and
needs that preset configured first.
