---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0007. ankerl::unordered_dense for the order-id index

## Context and Problem Statement

Cancel and modify paths take an `order_id_t` and must return the
corresponding `order*` in single-digit nanoseconds. The hash map must
not allocate per insertion in the steady state, must not have huge
rehash spikes during bursts, and must be cache-friendly under
collision.

## Decision Drivers

- O(1) lookup on the hot path, ideally one or two cache lines.
- No per-insert allocation once warmed.
- Stable performance under bursty inserts and deletes.
- Familiar to reviewers; not a hand-rolled hash table.
- Available from vcpkg.

## Considered Options

- `ankerl::unordered_dense::segmented_map` (header-only, vcpkg-shipped).
- Hand-rolled robin-hood open-addressing map.
- `absl::flat_hash_map`.
- `tsl::robin_map`.
- `std::unordered_map`.

## Decision Outcome

Chosen option: **`ankerl::unordered_dense::segmented_map<order_id_t,
order*>`**, with `wyhash` for `order_id_t`. Segmented variant chosen
specifically to avoid the giant single allocation on rehash spikes
that the flat variant exhibits under bursts.

### Consequences

- Positive: Lookup is one or two cache-line probes typical, well under
  10 ns warm.
- Positive: Segmented backing avoids giant single allocations on
  rehash; bursts do not cause latency cliffs.
- Positive: Header-only; no extra link-time dep.
- Positive: vcpkg ships it; pinning is trivial.
- Negative: Hand-tuned hash maps could shave a few cycles further but
  give up reviewer familiarity.
- Risk: Future requirement for snapshot serialisation may need a
  custom backing; the `id_index` class wraps the map so the impl can
  swap without touching call sites.

## Pros and Cons of the Options

### ankerl::unordered_dense::segmented_map

- Pro: Best-in-class open-addressing implementation.
- Pro: Segmented backing prevents giant rehash allocations.
- Pro: Header-only.
- Pro: Used widely in low-latency C++.

### Hand-rolled robin-hood

- Pro: Could shave further cycles by specialising for `uint64_t` keys.
- Con: Extra code to maintain and review.
- Con: Reinvents what `unordered_dense` already does correctly.

### absl::flat_hash_map

- Pro: Google-quality implementation.
- Con: Pulls in the Abseil dependency, which is heavy.
- Con: Tied to Abseil release cadence.

### tsl::robin_map

- Pro: Battle-tested.
- Con: Older; benchmarks usually trail `unordered_dense`.

### std::unordered_map

- Pro: Standard library; no dependency.
- Con: Chained buckets, allocates per insert.
- Con: Cache-unfriendly.
- Con: Routinely 2-3 times slower than open-addressing alternatives.

## More Information

- ankerl::unordered_dense: <https://github.com/martinus/unordered_dense>
- wyhash: <https://github.com/wangyi-fudan/wyhash>
- Related: [ADR-0006](0006-slab-arena-intrusive-fifo.md) (the `order*`
  values returned by this map live in the slab arena).
