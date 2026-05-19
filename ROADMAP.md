# Roadmap

This document tracks high-level direction. For granular work see the [issue tracker](https://github.com/ZacKienzle2/Orderbooks/issues) and [project boards](https://github.com/ZacKienzle2/Orderbooks/projects).

Architectural decisions referenced below live in [`docs/adr/`](docs/adr/README.md). The roadmap is intentionally aspirational. Items are not commitments. Priorities shift as the project learns from users and contributors.

## Now

In active development.

- Single-symbol matching engine with strict price-time priority, dense tick-ladder book, hierarchical bitmap for best-price lookup, slab arena, intrusive FIFOs, robin-hood id index, SPSC ingress and egress rings, time-in-force coverage for GTC / IOC / FOK, configurable self-cross policy.

## Next

Planned for the next milestone.

- Huge-page-backed arena polish.
- Deterministic snapshot and warm-start sink.
- Multi-symbol shard router with per-core pinning.
- MPSC ingress option for multi-gateway deployments.
- Post-only and pegged time-in-force variants (preceded by a top-of-book listener seam).

## Later

Under consideration. Open issues to discuss.

- Zero-copy FIX 4.4 tag-value parser over `std::span<const std::byte>`, gateway adapter writing into the command ring, libFuzzer harness for malformed inputs.
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
