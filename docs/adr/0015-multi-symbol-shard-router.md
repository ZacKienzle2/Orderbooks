---
status: "Accepted"
date: "2026-05-20"
deciders: ["Zac Kienzle"]
---

# 0015. Multi-symbol shard router over per-symbol engines

## Context and Problem Statement

The single-symbol engine satisfies the price-time priority guarantee
within one instrument but does not scale to a venue's full symbol set.
A single-threaded engine handling thousands of symbols would serialise
unrelated order flow and waste cores. The cross-symbol dispatch layer
needs to be cheap on the hot path and structurally separate from the
engine itself so the engine stays single-threaded and easy to reason
about.

## Decision Drivers

- Cross-symbol dispatch must add a small constant per command, not
  proportional to the number of shards or the number of symbols.
- Each shard must be an independent engine: independent book, arena,
  id_index, and seq counter. State isolation eliminates cross-symbol
  cache contention and makes per-shard pinning straightforward.
- The dispatch decision must be deterministic so the same symbol
  always lands on the same shard across runs and across snapshots.
- The router itself must compose with the existing engine without
  modifying engine internals.

## Considered Options

- SplitMix64 hash on symbol_id, truncated to log2(NumShards) bits.
- std::hash<symbol_id_t> with a modulus.
- Pre-computed symbol-to-shard table updated by the gateway.
- Reactor pattern: single thread reading a queue of (symbol, command)
  records and dispatching by switch.

## Decision Outcome

Chosen option: **SplitMix64 truncated to log2(NumShards) bits**, with
NumShards constrained at compile time to a power of two so the modulus
is a mask. NumShards is a template parameter on the router; the engine
type itself is unchanged.

```cpp
template <publisher P, std::size_t Ticks, std::size_t MaxOrders,
          std::size_t NumShards>
class shard_router {
    static_assert(std::has_single_bit(NumShards));
    // shard_for(sym) = splitmix64(sym) & (NumShards - 1);
};
```

The dispatch step is one multiplication and two xor-shifts on the
symbol id, masked to the shard count: roughly fifteen cycles on a
modern x86 core, negligible against the engine's per-command cost.

The router holds the shards as
`std::array<std::unique_ptr<engine_type>, NumShards>` because the
engine is intentionally non-movable; the unique_ptr indirection costs
one cache miss on the first command per shard and zero thereafter.

### Consequences

- Positive: dispatch is mask-only; no division, no table lookup.
- Positive: SplitMix64 gives full avalanche so contiguous symbol id
  ranges spread evenly across shards.
- Positive: each shard is an entirely independent engine, snapshotted
  and restored individually; the router has nothing to serialise.
- Positive: threading remains a wrapper concern; the router is
  single-threaded by design and pins via the caller's sched_setaffinity
  call (Linux) or thread_policy_set (macOS).
- Negative: changing NumShards changes the shard assignments;
  warm-starts across different NumShards configurations require a
  migration step.
- Negative: per-shard order_id namespaces; cross-shard order_id
  uniqueness is a gateway-side property.

## Pros and Cons of the Options

### SplitMix64 + mask

- Pro: trivially fast; full avalanche; ASCII representable code.
- Pro: deterministic; identical on every host.
- Pro: no external table or state to maintain.
- Con: power-of-two constraint on NumShards.

### std::hash + modulus

- Pro: idiomatic.
- Con: distribution quality depends on the standard library
  implementation (some lower bits are constant for trivial keys).
- Con: modulus is a division when NumShards is not a power of two.

### Pre-computed symbol-to-shard table

- Pro: arbitrary shard counts, arbitrary symbol-id formats.
- Pro: gateway can rebalance hot symbols off contested shards.
- Con: table is mutable state shared with the gateway; introduces a
  coordination protocol the router has to participate in.
- Con: table lookup is a cache miss per command in the steady state.

### Reactor pattern

- Pro: simplest threading model.
- Con: serialises across symbols; defeats the purpose of sharding.

## More Information

- Implementation: `include/lob/shard_router.hpp`.
- Tests: `tests/test_shard_router.cpp` covers deterministic dispatch,
  distribution quality across four shards and 4'000 symbols, state
  isolation between two distinct-shard symbols, end-to-end matching
  when the same symbol crosses, and a 200-command stress that cancels
  and modifies every order and verifies every shard ends empty.
- Related: [ADR-0008](0008-single-thread-engine-spsc-boundary.md)
  (the single-thread + SPSC boundary that this router composes over).
