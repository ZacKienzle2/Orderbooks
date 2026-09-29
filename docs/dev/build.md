# Build

## Prerequisites

CMake 3.30+, a C++20 compiler (Clang 17+, GCC 13+, Apple Clang 15+), Conan 2,
`uv`, `just`. The `Brewfile` names them for macOS, and `brew bundle` installs
them. Conan brings Ninja itself, as a tool requirement of `conanfile.py`.

## First-time setup

```bash
git clone https://github.com/ZacKienzle2/Orderbooks
cd Orderbooks

brew bundle                          # the Brewfile: cmake, conan, llvm, ccache, uv, prek, just
uv sync
prek prepare-hooks                   # every formatter and linter, into prek's cache

export CC=clang CXX=clang++          # the compiler the profile and CMake both take
conan profile detect
```

Linux contributors: the same packages from `apt-get` or `dnf`, and Conan from
`pipx` or `uv tool`, then the same `uv`, `prek` and `conan` commands.

## Configurations

`conanfile.py` generates the toolchain and the CMake presets. `conan build`
installs the dependencies at the versions `conan.lock` pins, then configures,
builds and runs the tests. Each recipe option sets the `LOB_*` cache variable of
the same meaning, and `cmake_layout` names the build folder and the presets
after the options given and the build type.

```bash
conan build . -s:a compiler.cppstd=20 --build=missing \
  -s build_type=Debug -o "&:sanitizer=address,undefined"
```

builds in `build/sanitizer_address,undefined/Debug`, with the configure, build
and test presets `conan-sanitizer_address,undefined-debug`. The CI jobs in
`.github/workflows/cmake.yml` list the configurations that CI builds for each
pull request. `just build` takes the same arguments. `conan install` alone
writes the toolchain and presets, after which
`cmake --preset conan-<folder>-<build type>` and `cmake --build --preset` work
as usual. A value outside the configuration, such as a path, goes through
`-c "tools.cmake.cmaketoolchain:extra_variables={'LOB_PGO_PROFILE': '...'}"`.

## Targets

| Target      | Description                                            |
| ----------- | ------------------------------------------------------ |
| `lob_core`  | Engine + book + arena + bitmap + ID index + SPSC ring. |
| `lob_tests` | Catch2 v3.                                             |
| `lob_bench` | Google Benchmark.                                      |

## Options

| Option                 | Conan option | Default | Effect                                                                 |
| ---------------------- | ------------ | ------- | ---------------------------------------------------------------------- |
| `LOB_ENABLE_LTO`       | `lto`        | OFF     | Link-time optimisation.                                                |
| `LOB_ENABLE_NATIVE`    |              | OFF     | `-march=native` instead of `x86-64-v3`.                                |
| `LOB_ENABLE_HARDENING` |              | ON      | Stack protector, CET, FORTIFY_SOURCE.                                  |
| `LOB_SANITIZER`        | `sanitizer`  | ""      | Comma list: `address,undefined`, `thread`, `memory`, `fuzzer,address`. |
| `LOB_BUILD_TESTS`      | `tests`      | ON      | Build `lob_tests`.                                                     |
| `LOB_BUILD_BENCH`      | `bench`      | ON      | Build `lob_bench`.                                                     |
| `LOB_BUILD_FUZZ`       | `fuzz`       | OFF     | Build libFuzzer harnesses.                                             |
| `LOB_COVERAGE`         | `coverage`   | OFF     | llvm-cov source-based coverage.                                        |
| `LOB_PGO`              | `pgo`        | off     | Profile-guided optimisation: `generate` or `use`.                      |

## Formatting and linting

```bash
just format            # clang-format and cmake-format, through the hooks
just lint              # run-clang-tidy over build/lto_on/Release/compile_commands.json
prek run --all-files
```

`just lint <folder>` reads another build's compilation database, which that
build writes when it configures.
