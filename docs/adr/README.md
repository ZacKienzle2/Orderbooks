# Architecture Decision Records

This directory holds the architectural decisions for Orderbooks, recorded as
[Markdown Architecture Decision Records (MADR v3)](https://adr.github.io/madr/).

## Why ADRs

An ADR captures one architectural decision, the alternatives considered, and the reasoning behind the chosen option. Each decision lives in its own file so reviewers can find, challenge, and supersede individual calls without rewriting a wall of prose.

## File naming

`NNNN-short-title-kebab-case.md`, where `NNNN` is a 4-digit zero-padded sequence number assigned at creation time and never reused. Examples:

- `0001-cpp20-baseline.md`
- `0007-unordered-dense-id-index.md`

Four digits gives headroom for a thousand-plus ADRs while preserving file-system sort order.

## Lifecycle

Each ADR carries a `Status` field:

- **Proposed** - drafted, under review, not yet adopted.
- **Accepted** - the decision is in force.
- **Deprecated** - no longer recommended; retained for historical context.
- **Superseded by ADR-NNNN** (link to the replacing ADR) - replaced by a later decision.

Never edit an Accepted ADR to change its outcome. Write a new ADR that supersedes it and update the old one's status. This preserves the decision history.

## Writing a new ADR

Scaffold a numbered, dated, status-stamped ADR with the helper:

```bash
./scripts/adr-new.sh "Use AVX-512 fast-path on supporting hosts"
```

This drops a new `NNNN-use-avx-512-fast-path-on-supporting-hosts.md` under `docs/adr/` from `template.md`, with `NNNN` set to the next free number.

Editing by hand: copy `template.md` to the next free number, fill in the placeholders, link any related ADRs, and update the index below.

## Index

| ID | Status | Title |
| --- | --- | --- |
| [0000](0000-record-architecture-decisions.md) | Accepted | Record architecture decisions |
| [0001](0001-cpp20-baseline.md) | Accepted | C++20 as the language baseline |
| [0002](0002-cmake-vcpkg-manifest-mode.md) | Accepted | CMake 3.28 with vcpkg manifest mode |
| [0003](0003-linux-x86-64-primary-macos-dev.md) | Accepted | Linux x86_64 primary target, macOS for development |
| [0004](0004-dense-tick-ladder-book.md) | Accepted | Dense tick-ladder order book representation |
| [0005](0005-hierarchical-bitmap-best-price.md) | Accepted | Hierarchical bitmap for best-price queries |
| [0006](0006-slab-arena-intrusive-fifo.md) | Accepted | Slab arena with intrusive FIFOs for orders |
| [0007](0007-unordered-dense-id-index.md) | Superseded by ADR-0017 | ankerl::unordered_dense for the order-id index |
| [0008](0008-single-thread-engine-spsc-boundary.md) | Accepted | Single-threaded engine with SPSC boundary rings |
| [0009](0009-crtp-publisher-no-virtual-hot-path.md) | Accepted | CRTP and concepts in place of virtual functions on the hot path |
| [0010](0010-conventional-commits-git-cliff-changelog.md) | Accepted | Conventional Commits with git-cliff-generated CHANGELOG |
| [0011](0011-tif-coverage-gtc-ioc-fok.md) | Accepted | Time-in-force coverage: GTC, IOC, FOK |
| [0012](0012-self-cross-policy-configurable.md) | Accepted | Self-cross policy is a construction-time configuration |
| [0013](0013-account-aware-self-cross.md) | Accepted | Account-aware self-cross detection and enforcement |
| [0014](0014-snapshot-wire-format.md) | Accepted | Snapshot wire format for warm-start |
| [0015](0015-multi-symbol-shard-router.md) | Accepted | Multi-symbol shard router |
| [0016](0016-numa-first-touch-arena.md) | Accepted | NUMA-correct first-touch initialisation for the slab arena |
| [0017](0017-soa-open-addressed-id-index.md) | Accepted | Open-addressed SoA hash table for the id_index |
| [0018](0018-zero-copy-fix-order-entry-parser.md) | Accepted | Zero-copy FIX 4.4 order-entry parser |
| [0019](0019-threaded-shard-runtime-core-pinned-workers.md) | Accepted | Threaded shard runtime with core-pinned workers |
| [0020](0020-per-shard-egress-rings.md) | Accepted | Per-shard egress rings for the threaded runtime |
| [0021](0021-merging-egress-consumer.md) | Accepted | Single-threaded merging egress consumer |
| [0022](0022-publisher-seam-for-merged-egress.md) | Accepted | Publisher-concept seam for the merged egress stream |
| [0023](0023-hugepage-backed-arena.md) | Accepted | Huge-page-backed slab arena |
| [0024](0024-hdr-latency-histogram.md) | Accepted | From-scratch HDR histogram for latency measurement |
| [0025](0025-match-sweep-prefetch.md) | Superseded by ADR-0027 | Software prefetch and invariant hoist in the match sweep |
| [0026](0026-absolute-latency-ceiling-gate.md) | Accepted | Absolute latency-ceiling gate in CI |
| [0027](0027-match-sweep-prefetch-reverted.md) | Accepted | Match-sweep prefetch measured and reverted |
| [0028](0028-in-place-modify-relink.md) | Accepted | In-place relink for a resting price-move modify |

## References

- Nygard, M. (2011). *Documenting Architecture Decisions*. <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs. <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
