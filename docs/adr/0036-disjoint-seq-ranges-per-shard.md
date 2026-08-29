---
status: "Accepted"
date: "2026-08-29"
deciders: ["Zac Kienzle"]
---

# 0036. Shards seed disjoint event-sequence ranges

## Context and Problem Statement

Every shard's engine counted its event sequence from zero. With the shared
publisher of `shard_router`, and after the per-shard streams of
`shard_egress_runtime` meet in the merger, seq stamps collided across shards,
so a downstream could neither order nor deduplicate by them.

## Decision Drivers

- Engine seq stamps should be globally unique in any merged stream without a
  relabelling pass on the hot path.
- The per-shard engine must stay ignorant of sharding; it already takes its
  configuration at construction.
- Snapshot restore carries an absolute seq and must keep working unchanged.

## Considered Options

- Seed each shard's engine with a disjoint range at construction.
- Restamp events centrally in the merger.
- Carry a shard id next to the seq in every event.

## Decision Outcome

Chosen option: **seed disjoint ranges**. `engine_config::seq_base` seeds the
counter at construction and on clear, and `lob::shard_seq_base(i, n)`
partitions the 64-bit space into n ranges of 2^(64 - log2 n). The router and
the egress runtime seed every shard, overriding any caller-supplied base. A
restore still adopts the snapshot header's absolute seq.

Central restamping was rejected because it serialises a per-event write
through the merger and destroys the engine-local meaning of the stamp.
Carrying a shard id widens every event for a field the seq can encode for
free in its high bits.

### Consequences

- Positive: merged streams keep globally unique, per-shard-monotonic stamps;
  the merger's gap-free merge sequence remains a separate counter.
- Positive: zero hot-path cost; the base is applied at construction.
- Negative: a snapshot restored into a different shard slot keeps its original
  range, a contract the operator owns.
