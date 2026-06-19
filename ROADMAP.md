# Roadmap

This document tracks high-level direction. For granular work see the [issue tracker](https://github.com/ZacKienzle2/Orderbooks/issues) and [project boards](https://github.com/ZacKienzle2/Orderbooks/projects).

Architectural decisions referenced below live in [`docs/adr/`](docs/adr/README.md). The roadmap is intentionally aspirational. Items are not commitments. Priorities shift as the project learns from users and contributors.

## Now

Active polish and observability work.

- Bench baseline captured on a pinned Linux host and committed as `bench/baseline.json` so the relative regression gate joins the now-live absolute latency-ceiling gate (ADR-0026), adding slow-creep detection on top of gross-jump detection.
- A gateway adapter that frames parsed FIX commands into a runtime ingress ring.

## Recently Landed

- Threaded shard runtime (see [ADR-0019](docs/adr/0019-threaded-shard-runtime-core-pinned-workers.md)) running one worker per shard, each pinned to its own core, draining a dedicated SPSC ingress ring into its engine; host tuning documented in [`docs/dev/threading.md`](docs/dev/threading.md).
- Per-shard egress rings (see [ADR-0020](docs/adr/0020-per-shard-egress-rings.md)) so each shard publishes events into its own SPSC ring through a `ring_publisher`, removing the thread-safe shared-publisher requirement.
- Merging egress consumer (see [ADR-0021](docs/adr/0021-merging-egress-consumer.md)) that fans the per-shard event rings into one sequenced stream for a single downstream sink.
- Publisher-concept seam (see [ADR-0022](docs/adr/0022-publisher-seam-for-merged-egress.md)) bridging the merged stream onto any publisher, wiring the runtime through the merger into the JSON Lines recorder.
- Huge-page-backed slab arena (see [ADR-0023](docs/adr/0023-hugepage-backed-arena.md)) preferring 2 MiB pages to cut data-TLB pressure during bursts, with a transparent fallback chain and no change to the first-touch NUMA policy.
- From-scratch HDR latency histogram (see [ADR-0024](docs/adr/0024-hdr-latency-histogram.md)) with O(1) allocation-free record and exact p50 / p99 / p99.9 / p99.99 queries, the in-process backbone for latency measurement and a future regression gate.
- Absolute latency-ceiling gate (see [ADR-0026](docs/adr/0026-absolute-latency-ceiling-gate.md)) over the engine latency benchmark, baseline-free and active on every CI run, failing the build when p50 or p99.9 exceeds a fixed reference-cycle ceiling.
- Match-sweep prefetch measured and reverted (see [ADR-0027](docs/adr/0027-match-sweep-prefetch-reverted.md)). An A/B benchmark showed the prefetch proposed in ADR-0025 regressed the common contiguous sweep and gained nothing on the scattered case it targeted, so the engine change was reverted; the deep-sweep benchmark that exposed it stays.
- Replay animation in the Python visualisation harness via `matplotlib.animation.FuncAnimation`.
- Matching engine with strict price-time priority, dense tick-ladder book, hierarchical bitmap (best-price queries and successor / predecessor walks), slab arena, intrusive FIFOs, robin-hood id index, SPSC ingress and egress rings, GTC / IOC / FOK time-in-force, account-aware self-cross policy with three behaviours.
- Snapshot and warm-start wire format (see [ADR-0014](docs/adr/0014-snapshot-wire-format.md)) with round-trip, warm-start-equivalence, and rejection-path tests.
- Multi-symbol shard router (see [ADR-0015](docs/adr/0015-multi-symbol-shard-router.md)) over per-symbol engines, dispatched via SplitMix64 truncated to log2(NumShards) bits.
- JSON Lines event recorder and `lob_replay` CLI; Python `orderbooks_viz` harness with six matplotlib renderers and a Streamlit dashboard.

## Next

Planned for the next milestone.

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
- AVX-512 fast-path for bitmap scans. Analysed and declined. The hierarchical bitmap (ADR-0005) already resolves best-price in O(tier-count) through count-trailing-zeros and count-leading-zeros over single words, so there is no linear scan for a SIMD path to beat, and a wide compare would add broadcast and movemask setup without removing a loop. Hot-path effort went instead to the match-sweep software prefetch (ADR-0025). Worth revisiting only if a future structure introduces an actual linear bitmap walk.

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
