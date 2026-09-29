---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0057. Compare the C++ build with a cmake-init project

## Context and Problem Statement

The configuration rule gives cmake-init as the starting point for C++. The C++
build here, meaning `CMakePresets.json`, `cmake/` and the C++ workflows, came
from no generator. scientific-python/cookie renders the Python side, and
`copier update` merges each template change into them. This record compares a
cmake-init project with the hand-written files and leaves the migration to its
owner.

## Method

cmake-init 0.41.1 generated a header-only C++20 project with vcpkg, from
`cmake-init -h --std 20 -p vcpkg` with every prompt left at its default. Its 35
files were read against the build on `main`.

## Findings

| Aspect                 | cmake-init 0.41.1                                                                                                                         | This repository                                                                  |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| Update                 | The README documents creating a project, and `--overwrite` to skip the check for a non-empty root. It documents no command to update one. | Hand-written                                                                     |
| Presets schema         | Version 2. Workflow presets arrived in version 6, with CMake 3.25.                                                                        | Version 6, with five workflow presets from pull request 122                      |
| Warnings and hardening | Typed into `CMAKE_CXX_FLAGS` in three presets, 23 flags for GCC and Clang, 20 for Apple Clang and 37 for MSVC                             | `target_compile_options` in `cmake/CompilerWarnings.cmake` and `Hardening.cmake` |
| Parallelism            | `"jobs": 8` in the user presets and `-j 2` on each CI build and test line                                                                 | CMake's and CTest's defaults                                                     |
| Coverage               | A custom target that runs `lcov` and `genhtml`. The README picks it over CTest's coverage step.                                           | A `justfile` recipe over `llvm-cov`                                              |
| clang-tidy             | A preset that sets `CMAKE_CXX_CLANG_TIDY`                                                                                                 | cpp-linter in `cmake.yml`, over the files a pull request changes                 |
| CI actions             | Pinned to major tags, with `pip3 install` and `apt-get` in run steps                                                                      | Pinned to commits by pinact                                                      |
| Documentation          | m.css, which the README says fails with Doxygen 1.9 and later                                                                             | Doxygen with warnings as errors                                                  |

## Decision Drivers

- One template writes a file, and the template keeps it through each update.
- A value takes the tool's default, and a pin comes from pinact.
- CMake's `CMAKE_<LANG>_CLANG_TIDY` property comes before an analyser command
  typed by hand.

## Considered Options

- Generate over the repository with `cmake-init --overwrite` and port the
  engine's targets into the result.
- Keep the hand-written build and take the clang-tidy preset that cmake-init
  demonstrates.
- Keep the hand-written build unchanged.

## Decision Outcome

The proposal is the second option. cmake-init has no update path. Its files
would be hand-written from their first commit. They would also start with the
flag lists, job counts and major-tag pins that the rules remove. The
`CMAKE_CXX_CLANG_TIDY` preset is the one piece the rules already prefer.

The configuration rule's C++ line is the owner's to change, since cmake-init
maintains no file after it writes it.

### Consequences

- Positive: the build keeps presets version 6, which workflow presets need.
- Negative: the C++ values keep the hand-written origin that ADR-0053 traces.
- Risk: clang-tidy through `CMAKE_CXX_CLANG_TIDY` runs on every build of that
  preset, which lengthens those builds.

## More Information

- Related: ADR-0053, which traces the literals this build keeps.
- cmake-init README, <https://github.com/friendlyanon/cmake-init>
- CMake 3.25 release notes, presets schema version 6,
  <https://cmake.org/cmake/help/latest/release/3.25.html>
- CMake `CXX_CLANG_TIDY`,
  <https://cmake.org/cmake/help/latest/prop_tgt/LANG_CLANG_TIDY.html>
