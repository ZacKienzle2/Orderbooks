---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0001. C++20 as the language baseline

## Context and Problem Statement

The engine targets sub-microsecond latencies and relies heavily on
template metaprogramming, concepts, and constexpr evaluation. The
language standard determines what idioms the codebase can use, which
compilers are eligible, and what reviewers will recognise.

## Decision Drivers

- Need first-class concepts to constrain template parameters without
  SFINAE noise; the global project conventions mandates Concepts over SFINAE.
- Need `std::span`, three-way comparison, `consteval`, designated
  initialisers, ranges for cleaner kernel code.
- Toolchain availability on Linux production hosts and macOS dev hosts.
- Avoid bleeding-edge features whose Clang or GCC support is patchy.

## Considered Options

- C++17 strict.
- C++20.
- C++23.

## Decision Outcome

Chosen option: **C++20**, because it gives concepts, span, three-way
comparison, and designated initialisers - the ergonomic wins that
matter for engine readability - while remaining fully supported by
Clang 17+ and GCC 13+ on every target host.

### Consequences

- Positive: Concepts replace verbose SFINAE in `Publisher`, `Clock`,
  `SnapshotSink` boundary contracts.
- Positive: `std::span` removes the gsl::span dependency.
- Positive: `consteval` lets the build assert engine invariants at
  compile time.
- Negative: Cannot use C++23-only `std::to_underlying`, `std::expected`,
  `std::mdspan`, deducing-this. Substitute with manual casts and
  open-coded equivalents until C++23 toolchain support is universal.
- Risk: A future ADR may upgrade to C++23 once GCC 14 and Clang 18 are
  ubiquitous on the production fleet.

## Pros and Cons of the Options

### C++17

- Pro: Maximum compiler portability.
- Con: No concepts; template constraints fall back to SFINAE and
  `enable_if`, hurting both readability and compiler error quality.
- Con: No `std::span`; depend on `gsl::span` or `boost::span`.
- Con: Resume bullet says "C++17 baseline" but doing C++17 strict gives
  up real ergonomic wins for zero portability gain on modern toolchains.

### C++20

- Pro: Concepts, span, three-way comparison, designated initialisers,
  ranges, calendar / time-zone library, atomic shared_ptr.
- Pro: Wide compiler support (GCC 11+, Clang 14+, MSVC 19.30+).
- Pro: Standardises features the global project conventions prefers.
- Con: A handful of library features (modules, coroutines) still uneven
  across stdlibs; this project does not require them.

### C++23

- Pro: `std::expected`, `std::mdspan`, `std::flat_map`, deducing this,
  `if consteval`, monadic optional/expected.
- Con: GCC 14 / Clang 18 minimum; CI runners and Apple Clang lag.
- Con: Production hosts on RHEL / Ubuntu LTS may not have GCC 14 stable
  for months.

## More Information

- Related: [ADR-0003](0003-linux-x86-64-primary-macos-dev.md) for the
  toolchain matrix this decision constrains.
- Related: [ADR-0009](0009-crtp-publisher-no-virtual-hot-path.md) for
  how concepts feed the CRTP boundary.
