---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0002. CMake 3.28 with vcpkg manifest mode

## Context and Problem Statement

The build must be reproducible on Linux production hosts, macOS dev
machines, and Linux CI runners across two compiler families. Third-party
dependencies (Boost.Intrusive, Catch2 v3, Google Benchmark, fmt,
unordered-dense, RapidCheck, nanobench) must be pinned to exact
versions and installed without polluting the system.

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

- Positive: Single source of truth (`vcpkg.json`) for dependencies +
  their version constraints + their feature gating.
- Positive: Presets give every contributor identical configure
  invocations across Linux and macOS.
- Positive: GitHub Actions cache (`x-gha`) accelerates CI dramatically.
- Positive: Toolchain file from vcpkg handles cross-platform find_package
  bindings.
- Negative: vcpkg manifest mode requires `VCPKG_ROOT` to be set; CI
  workflows must install vcpkg before configure.
- Negative: First clean build is slow because vcpkg compiles every
  dependency from source; mitigated by the GHA cache.

## Pros and Cons of the Options

### CMake + vcpkg manifest

- Pro: Manifest mode pins dependencies per project, no global state.
- Pro: Microsoft-maintained, large registry, security advisories.
- Pro: Presets v6 supports condition expressions, inheritance, env vars.
- Con: Compiles from source by default (large first-build cost).
- Con: Triplet system has a small learning curve.

### CMake + Conan 2

- Pro: Profiles allow more granular cross-compilation.
- Pro: Binary cache servers are easier to self-host.
- Con: Two ways to declare dependencies (recipes vs requires) is
  confusing for reviewers.
- Con: Python tool with its own venv churn.

### CMake + FetchContent only

- Pro: Zero external tooling; CMake fetches and configures everything.
- Pro: Simplest CI setup.
- Con: No dependency pinning across builds; every contributor downloads
  the same archive from scratch.
- Con: No package cache; CI runs balloon.
- Con: No security advisory feed.

### Bazel

- Pro: Hermetic builds, remote caching, fine-grained incrementality.
- Con: Most C++ HFT-style review audiences expect CMake.
- Con: vcpkg / Conan-equivalent dependency story is heavier in Bazel.
- Con: IDE integration is uneven outside Google ecosystem.

### Meson + wraps

- Pro: Fast configure, clean syntax.
- Con: Smaller ecosystem; fewer C++ libraries ship native Meson
  configurations.
- Con: Fewer reviewers will be fluent.

## More Information

- vcpkg manifest mode: <https://learn.microsoft.com/vcpkg/users/manifests>
- CMakePresets v6 schema: <https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html>
- Related: [ADR-0003](0003-linux-x86-64-primary-macos-dev.md).
