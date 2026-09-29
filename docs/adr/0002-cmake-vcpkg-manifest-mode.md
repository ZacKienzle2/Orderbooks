---
status: "Superseded by ADR-0059"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0002. CMake 3.28 with vcpkg manifest mode

## Context and Problem Statement

The build must be reproducible on Linux production hosts, macOS dev machines,
and Linux CI runners across two compiler families. Third-party dependencies
(Boost.Intrusive, Catch2 v3, Google Benchmark, fmt, unordered-dense, RapidCheck,
nanobench) must be pinned to exact versions and installed without polluting the
system.

## Decision Drivers

- Reproducibility across hosts and CI.
- IDE integration (CLion, VS Code, Vim with cmake-tools).
- Per-developer preset switching without editing `CMakeLists.txt`.
- Lock-file semantics for dependencies.
- Familiarity to third-party reviewers.

## Considered Options

- CMake + vcpkg in manifest mode.
- CMake + Conan 2.
- CMake + FetchContent only.
- Bazel.
- Meson + wraps.

## Decision Outcome

Chosen option: **CMake 3.28 + vcpkg manifest mode**, configured through
`CMakePresets.json` v6. Dependency baseline pinned by commit SHA in
`vcpkg-configuration.json`; features (`tests`, `bench`) toggle Catch2 /
RapidCheck / Google Benchmark / nanobench in or out.

### Consequences

- Positive: one file (`vcpkg.json`) declares the dependencies, their version
  constraints and their feature gating.
- Positive: presets give every contributor identical configure invocations
  across Linux and macOS.
- Positive: the GitHub Actions cache (`x-gha`) accelerates CI dramatically.
- Positive: the toolchain file from vcpkg handles cross-platform find_package
  bindings.
- Negative: vcpkg manifest mode requires `VCPKG_ROOT` to be set, and CI
  workflows must install vcpkg before configure.
- Negative: the first clean build is slow because vcpkg compiles every
  dependency from source. The GHA cache mitigates it.

## Pros and Cons of the Options

### CMake + vcpkg manifest

- Pro: manifest mode pins each project's dependencies in that project.
- Pro: maintained by Microsoft, with a large registry and security advisories.
- Pro: presets v6 supports condition expressions, inheritance and env vars.
- Con: compiles from source by default (large first-build cost).
- Con: the triplet system has a small learning curve.

### CMake + Conan 2

- Pro: profiles give finer control over cross-compilation.
- Pro: binary cache servers are easier to self-host.
- Con: declaring dependencies through both recipes and requires confuses
  reviewers.
- Con: a Python tool with its own venv churn.

### CMake + FetchContent only

- Pro: zero external tooling. CMake fetches and configures everything.
- Pro: simplest CI setup.
- Con: builds don't share a pinned dependency set. Every contributor downloads
  the same archive from scratch.
- Con: without a package cache, CI runs balloon.
- Con: without a security advisory feed.

### Bazel

- Pro: hermetic builds, remote caching, fine-grained incrementality.
- Con: most C++ HFT-style review audiences expect CMake.
- Con: vcpkg / Conan-equivalent dependency story is heavier in Bazel.
- Con: IDE integration is uneven outside Google ecosystem.

### Meson + wraps

- Pro: fast configure, clean syntax.
- Con: smaller ecosystem. Fewer C++ libraries provide native Meson
  configurations.
- Con: fewer reviewers will be fluent.

## More Information

- vcpkg manifest mode: <https://learn.microsoft.com/vcpkg/users/manifests>
- CMakePresets v6 schema:
  <https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html>
- Related: [ADR-0003](0003-linux-x86-64-primary-macos-dev.md).
