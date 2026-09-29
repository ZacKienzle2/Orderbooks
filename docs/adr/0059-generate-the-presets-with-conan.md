---
status: "Accepted"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0059. Generate the presets with Conan

## Context and Problem Statement

`CMakePresets.json` held 55 presets, every one typed, and vcpkg generates none
(ADR-0058). The owner chose Conan 2's `CMakeToolchain` generator, which writes
configure, build and test presets from a profile and the recipe's options, and
moves the dependencies from vcpkg to ConanCenter.

## Decision Outcome

`conanfile.py` starts from `conan new cmake_lib`, cut to what a consumer needs.
It requires Boost (header-only), HdrHistogram_c, Catch2, RapidCheck, magic_enum
and Google Benchmark at `[*]`, and Ninja as a tool requirement, which
`conan lock create` resolves into `conan.lock` for Linux and macOS. Each recipe
option passes one `LOB_*` cache variable through when it is given, so
`CMakeLists.txt` keeps the only defaults. `layout()` hands `cmake_layout` the
options given as `build_folder_vars`, so a plain Release build goes to
`build/Release` and an address-sanitised Debug build to
`build/sanitizer_address,undefined/Debug`, each with its own presets. A value
outside the configuration, such as a path (`LOB_PGO_PROFILE`) or a budget
(`LOB_FUZZ_SECONDS`), goes through `tools.cmake.cmaketoolchain:extra_variables`,
scoped to the consumer with the `&:` pattern, since Conan applies an unscoped
conf to every dependency it builds from source.

`conan profile detect` writes the profile for the compiler `CC` and `CXX` name.
`conan build` configures, builds and runs the tests through `CMake.ctest()`,
which runs in parallel over `tools.build:jobs`, and passes the
`--output-on-failure` and `--no-tests=error` the test presets set.

CI installs Conan through `conan-io/setup-conan` with `cache_packages`, and each
job is one `conan build`, or `conan install` then `cmake --preset` where a job
configures or builds one target. The composite action that set up vcpkg and
Ninja goes, with `CMakePresets.json`, `vcpkg.json` and
`vcpkg-configuration.json`. The justfile recipes pass Conan settings and options
in place of preset names.

### Measured on WSL (Ubuntu 24.04, clang 18, GCC 13)

| Configuration                               | Result                |
| ------------------------------------------- | --------------------- |
| clang, Debug                                | 165 of 165 tests pass |
| GCC, Debug                                  | 165 of 165 tests pass |
| clang, Debug, `sanitizer=address,undefined` | 165 of 165 tests pass |
| clang, Release, `lto=ON`                    | 165 of 165 tests pass |
| clang, `fuzz=ON` with the stored corpora    | 4 of 4 replays pass   |
| `just fuzz-all 5`                           | 173 of 173 tests pass |

The compile command for `tests/test_arena.cpp` matches the vcpkg preset's except
that Conan's toolchain adds `-m64` and `-stdlib=libstdc++`, both the compiler's
defaults on this target, and drops `-fcolor-diagnostics`.

### RapidCheck's Catch2 integration

ConanCenter's RapidCheck recipe builds its Catch2 integration only against
Catch2 2.13.10, and with Catch2 3 forced its build fails. The owner chose
RapidCheck's core API. An ast-grep rule rewrote the 11 `rc::prop` calls in 5
test files to `REQUIRE(rc::check(...))`, and `rapidcheck/catch.h` goes.

### Consequences

- Positive: every preset comes from Conan, and a new configuration needs only a
  profile or an option value.
- Negative: the generated presets don't include workflow presets, and
  `conan build` takes their place.
- Negative: without a Conan ecosystem in Dependabot, `conan.lock` moves only
  when `conan lock create` runs again.
- Negative: `setup-conan` keys its package cache by Conan version and runner OS
  alone. The Linux jobs share one entry. The first job to finish saves it, and
  the others build what it lacks.
- Risk: the Friday workflow references the old presets and moves with this
  change (ADR-0054).

## More Information

- Supersedes ADR-0002 (CMake with vcpkg manifest mode).
- Related: ADR-0057 (cmake-init), ADR-0058 (the inventory that found the typed
  presets).
- Conan 2, `CMakeToolchain` and `cmake_layout`,
  <https://docs.conan.io/2/reference/tools/cmake/cmaketoolchain.html>
- Conan 2, sanitizers, <https://docs.conan.io/2/security/sanitizers.html>
- Conan 2, the `CMake` build helper and `ctest()`,
  <https://docs.conan.io/2/reference/tools/cmake/cmake.html>
- `conan-io/setup-conan`, <https://github.com/conan-io/setup-conan>
- conan-center-index RapidCheck recipe,
  <https://github.com/conan-io/conan-center-index/tree/master/recipes/rapidcheck>
