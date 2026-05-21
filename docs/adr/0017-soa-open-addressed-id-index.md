---
status: "Accepted"
date: "2026-05-21"
deciders: ["Zac Kienzle"]
---

# 0017. Open-addressed SoA hash table for the id_index

## Context and Problem Statement

`id_index` resolves the order id supplied by every cancel and modify
into the `order*` resting in the slab arena. It sits on the hot path
and is the single most-touched indirection in the engine: every
cancel and every modify reads from it before any book mutation can
proceed.

The current implementation wraps `ankerl::unordered_dense::segmented_map`.
The segmented storage trades a small per-lookup cost (extra pointer
chase from the bucket array into a segment) for stable iterator
invalidation behaviour. Neither property matters to the engine:
nothing iterates the index, and capacity is bounded by `MaxOrders`
so rehash never fires after construction-time reserve.

The hot-path cost of the current design is dominated by two
unavoidable cache misses per lookup: one into the bucket array,
one into the segment that holds the key/value pair. On a typical
cancel-heavy workload that is the bulk of the cancel latency.

## Decision Drivers

- The hot path must minimise cache misses. SoA layout (a contiguous
  key array plus a contiguous value array) keeps one cache line per
  probe instead of two.
- The implementation must keep `insert`, `lookup`, `erase` `noexcept`
  and allocation-free on the hot path. Construction-time allocation
  is fine.
- The external API (insert, lookup, erase, size, empty, clear, ctor
  with capacity hint) must not change. Tests and call sites should
  pick up the new implementation without edits.
- The engine guarantees `MaxOrders` is the population ceiling.
  The table can be sized at construction time to keep the load
  factor under 0.5 and resize never fires.
- The implementation must remain header-only and free of platform
  conditionals. SIMD-accelerated probing is a worthwhile future
  optimisation but does not gate the SoA layout change.

## Considered Options

- **Open-addressed SoA table with linear probing and a sentinel
  empty key**: contiguous `keys_` and `values_` arrays sized to the
  next power of two above `2 * capacity`. SplitMix64 hash. Backward-
  shift deletion preserves probe-sequence tightness without
  tombstones.
- **Open-addressed AoS table with Robin Hood probing**: variance-
  bounded probe distance, simpler erase, but keys and values share
  a cache line so the probe touches both even when only the key is
  needed.
- **Keep ankerl::unordered_dense::segmented_map**: zero work, but
  leaves the two-cache-line probe in place permanently.
- **Custom flat hash with SIMD batch probing (e.g. AVX2 4-way
  `_mm256_cmpeq_epi64`)**: best-in-class lookup throughput when
  probe sequences are long; significant complexity (architecture
  conditional, edge cases at wrap-around).

## Decision Outcome

Chosen option: **open-addressed SoA table with linear probing and
backward-shift deletion**, sized to the next power of two above
`2 * capacity` so the load factor stays at or below 0.5.

The key array is `std::vector<order_id_t>` initialised to a sentinel
empty value (`~order_id_t{0}`); the value array is
`std::vector<order*>`. Lookup hashes the id with SplitMix64, masks
to the bucket index, and linear-probes until either a match or the
sentinel is encountered. With a load factor at or below 0.5, the
expected probe length is ~1.5 and the worst case is bounded.

Erase uses backward-shift deletion: after removing a slot, walk
forward and pull back any displaced entries until reaching an empty
slot. This keeps lookup probe distances short without introducing
tombstones, which would otherwise poison the probe sequence over
time.

Insert with a key already present overwrites the stored pointer.
The engine does not insert duplicates; the overwrite path is
defensive only.

SIMD batch probing is deliberately out of scope. The SoA layout
keeps the cache miss count down to one per probe; at load factor
0.5 the average lookup touches one or two contiguous keys, well
inside a single cache line, so AVX2 batch comparison would buy
little over scalar `==` on the keys already in registers. If
production profile shows otherwise, a follow-up ADR can revisit.

The sentinel reserves the single `order_id_t` value
`std::numeric_limits<order_id_t>::max()`. The engine, the gateway,
and the snapshot replay layer treat this id as reserved.

### Consequences

- Positive: lookup is one cache line per probe; cancel and modify
  latency drops by the amortised cost of the second miss.
- Positive: storage is two contiguous `std::vector`s of trivially
  copyable elements. Total memory is `(8 + 8) * Capacity` bytes
  plus rounding; smaller than the segmented_map's bucket+segment
  overhead for typical `MaxOrders`.
- Positive: removes a vendored header (ankerl::unordered_dense)
  from the hot path. The header stays available for any caller
  that wants a general-purpose ordered map but the engine no
  longer depends on it.
- Positive: SIMD batch probing remains available as a future
  refinement without changing the layout.
- Negative: caller may not use the reserved sentinel id.
  Documented in the header.
- Negative: tests must keep load factor under 0.5 to honour the
  bounded probe expectation; ctor enforces this by rounding up.
- Risk: backward-shift erase is more code than tombstone erase
  and an off-by-one can corrupt the probe sequence. Mitigated by
  the existing differential-against-std::unordered_map property
  test in `tests/test_id_index.cpp`, which exercises hundreds of
  random insert / lookup / erase sequences.

## Pros and Cons of the Options

### Open-addressed SoA + linear probing

- Pro: single cache line per probe; minimal indirection.
- Pro: contiguous arrays vectorise trivially in the future.
- Pro: backward-shift erase keeps probe sequences tight without
  tombstones.
- Pro: deterministic worst-case bound at controlled load factor.
- Con: sentinel key value is reserved; bad caller invariant if
  ignored.
- Con: linear probing degrades on adversarial hash patterns;
  mitigated by SplitMix64's full avalanche on uint64 inputs.

### Open-addressed AoS + Robin Hood

- Pro: variance-bounded probe distance under load.
- Pro: erase is simpler than backward-shift.
- Con: key and value share a cache line; probe touches both even
  when the value is not the immediate need.

### ankerl::unordered_dense::segmented_map (status quo)

- Pro: zero work to keep.
- Pro: stable iterators across rehash (the engine never relies on
  this).
- Con: two cache misses per lookup (bucket array, then segment).
- Con: dependence on a vendored header in a hot-path component.

### Custom SIMD batch hash

- Pro: best-in-class lookup throughput when probe sequences are
  long.
- Pro: scales to wider vector ISAs.
- Con: significant complexity; architecture conditional code
  paths.
- Con: limited gain at load factor 0.5 where probe sequences
  are short.

## More Information

- Implementation: `include/lob/id_index.hpp`.
- Related: [ADR-0006](0006-slab-arena-intrusive-fifo.md) (the
  arena that owns the `order` storage indexed by this table),
  [ADR-0007](0007-unordered-dense-id-index.md) (superseded by
  this ADR for the engine's hot-path index; the upstream library
  remains a valid choice for non-hot-path callers).
- Future work: SIMD batch probing (AVX2 `_mm256_cmpeq_epi64`
  on x86_64) once profile data justifies the architecture
  conditional.
