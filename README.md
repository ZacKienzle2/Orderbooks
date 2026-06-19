# Orderbooks

Low-latency limit order book and matching engine for high-frequency
trading simulation. C++20 over Boost.Intrusive, designed for
sub-microsecond order processing on Linux x86_64.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/build-CMake%203.28%2B-064F8C.svg?logo=cmake)](https://cmake.org/)
[![vcpkg](https://img.shields.io/badge/deps-vcpkg-0078D4.svg)](https://learn.microsoft.com/vcpkg/)
[![Conventional Commits](https://img.shields.io/badge/Conventional%20Commits-1.0.0-fe5196.svg)](https://www.conventionalcommits.org/en/v1.0.0/)
[![SemVer](https://img.shields.io/badge/SemVer-2.0.0-blue.svg)](https://semver.org/spec/v2.0.0.html)
[![pre-commit](https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit)](https://github.com/pre-commit/pre-commit)

<!-- CI, CodeQL, Scorecard and last-commit badges return on public visibility. -->

## Scope

### Engine

- Strict price-time priority FIFO matching at every level.
- Dense tick-ladder order book with hierarchical bitmap for `O(1)`
  best-bid / best-ask lookup.
- Time-in-force coverage: GTC, IOC, FOK. Post-only and pegged on the
  roadmap.
- Configurable self-cross policy: cancel-newest, cancel-oldest, decrement-trade.

### Allocation and data structures

- Slab arena over preallocated, cache-aligned storage; intrusive freelist
  removes runtime `new`/`delete` from the hot path.
- `boost::intrusive::list` FIFOs at each price level; no node allocation
  per order.
- Open-addressed robin-hood map (`ankerl::unordered_dense::segmented_map`)
  for order-id to order lookup on cancel and modify.
- Optional huge-page backing (2 MiB) on Linux for warm-cache, low-jitter
  steady-state operation.

### Concurrency

- Single-threaded engine pinned to an isolated core.
- Vyukov-style bounded SPSC ring at the ingress and egress boundaries,
  cache-line padded heads and tails, no false sharing.
- Multi-symbol scalability via per-symbol shard router over independent
  per-symbol engines.
- Threaded shard runtime that drives each shard on its own worker thread,
  pinned to its own core, draining a dedicated SPSC ingress ring.

### Wire format

- Zero-copy FIX 4.4 tag-value parser over `std::span<const std::byte>`,
  no allocations, no `std::string`. Tracked under a separate ADR.

### Determinism and recovery

- Monotonic sequence number on every command and event; replay from any
  prefix reproduces engine state bit-exactly.
- Snapshot sink serialises the book to a flat POD layout for warm-start.

### Observability

- Latency histograms via `nanobench` for tail-aware p50 / p99 / p99.9.
- `scripts/perfstat.sh` wraps `perf stat` for IPC, branch-miss and
  L1-miss telemetry under a fixed-seed workload.
- CI bench job gates throughput regressions against `bench/baseline.json`.

## Build

Requires CMake 3.28, vcpkg in manifest mode, and a C++20 compiler
(GCC 13+, Clang 17+, Apple Clang 15+).

```bash
git clone https://github.com/ZacKienzle2/Orderbooks
cd Orderbooks
cmake --preset linux-clang-rel
cmake --build --preset linux-clang-rel
ctest --preset linux-clang-rel --output-on-failure
```

macOS dev:

```bash
cmake --preset macos-clang-dev
cmake --build --preset macos-clang-dev
ctest --preset macos-clang-dev --output-on-failure
```

Benchmarks:

```bash
cmake --build --preset linux-clang-rel --target lob_bench
./build/linux-clang-rel/bench/lob_bench --benchmark_format=json | tee bench/last.json
```

## Tooling harness

A `.venv` exists for repo tooling only: `pre-commit`, `clang-format`,
`cmake-format`, `ruff`, `pytest` for harness scripts, `pandas` and
`matplotlib` for latency analysis. It is not a runtime dependency.

```bash
uv sync --frozen
uv run pre-commit install --install-hooks
```

## Repository layout

```text
include/lob/    public headers (header-only domain types + engine ABI)
src/lob/        translation units (build-only internals)
tests/          Catch2 v3 unit + property tests, reference engine, replay fixtures
bench/          Google Benchmark microbenches + nanobench tail reports
cmake/          warnings, sanitisers, hardening, dependencies modules
scripts/        perfstat, formatting, lint, replay helpers
docs/           design specs, dev guides, ADRs
```

## Maintainers

See [CODEOWNERS](.github/CODEOWNERS).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Conventional Commits 1.0.0 and
DCO sign-off required.

## License

[MIT](LICENSE).

## Related

[SECURITY](SECURITY.md) | [SUPPORT](SUPPORT.md) | [GOVERNANCE](GOVERNANCE.md) | [CHANGELOG](CHANGELOG.md) | [ROADMAP](ROADMAP.md) | [CITATION](CITATION.cff)
