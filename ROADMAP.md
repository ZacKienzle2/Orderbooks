# Roadmap

This document tracks high-level direction. For granular work see the [issue tracker](https://github.com/ZacKienzle2/Orderbooks/issues) and [project boards](https://github.com/ZacKienzle2/Orderbooks/projects).

Architectural decisions referenced below live in [`docs/adr/`](docs/adr/README.md). The roadmap is intentionally aspirational. Items are not commitments. Priorities shift as the project learns from users and contributors.

## Now

Active polish and observability work.

- Per-core pinning of shard threads via `sched_setaffinity` (Linux) and `thread_policy_set` (macOS), with documented host configuration for production-quality latency numbers.
- Replay animation in the Python visualisation harness via `matplotlib.animation.FuncAnimation`.
- Bench baseline captured on a pinned Linux host and committed as `bench/baseline.json` so the regression gate becomes live.

## Recently Landed

- Matching engine with strict price-time priority, dense tick-ladder book, hierarchical bitmap (best-price queries and successor / predecessor walks), slab arena, intrusive FIFOs, robin-hood id index, SPSC ingress and egress rings, GTC / IOC / FOK time-in-force, account-aware self-cross policy with three behaviours.
- Snapshot and warm-start wire format (see [ADR-0014](docs/adr/0014-snapshot-wire-format.md)) with round-trip, warm-start-equivalence, and rejection-path tests.
- Multi-symbol shard router (see [ADR-0015](docs/adr/0015-multi-symbol-shard-router.md)) over per-symbol engines, dispatched via SplitMix64 truncated to log2(NumShards) bits.
- JSON Lines event recorder and `lob_replay` CLI; Python `orderbooks_viz` harness with six matplotlib renderers and a Streamlit dashboard.

## Next

Planned for the next milestone.

- Huge-page-backed arena (`MAP_HUGETLB | MAP_HUGE_2MB`) on Linux with macOS fallback.
- MPSC ingress option for multi-gateway deployments.
- Post-only and pegged time-in-force variants (preceded by a top-of-book listener seam).
- Differential property tests over the shard router under multi-symbol load.

## Later

Under consideration. Open issues to discuss.

- Zero-copy FIX 4.4 tag-value parser over `std::span<const std::byte>`: done (`include/lob/fix_parser.hpp`, ADR-0018) for the NewOrderSingle / OrderCancelRequest / OrderCancelReplaceRequest order-entry subset, emitting `lob::command`. Remaining: gateway adapter writing parsed commands into the command ring, and a libFuzzer harness for malformed inputs.
- ITCH 5.0 feed handler and reference replay harness.
- L3 book egress publisher with delta compression.
- Risk gateway (pre-trade limits, fat-finger guards).
- Order-routing simulator with venue latency models.
- Python bindings via pybind11 for scripted backtests on the matching engine.
- AVX-512 fast-path for bitmap scans on hosts with that ISA.

## Out of Scope

Explicitly not on the roadmap. Open an issue to challenge if you disagree.

- Persistent storage of orders or fills as a primary use case (snapshots are for warm-start, not durability).
- Cross-venue arbitrage logic; this is a single-venue book and matcher.
- Decimal price representation; prices are integer ticks by design.
- Windows runtime support (development on Windows is unsupported; the engine is Linux-first with macOS as a development target).

## How to Influence the Roadmap

- Comment on items above with use cases.
- File feature requests using the [feature template](.github/ISSUE_TEMPLATE/feature_request.yml).
- Join discussions in [GitHub Discussions](https://github.com/ZacKienzle2/Orderbooks/discussions).
- Submit a pull request demonstrating the idea.
