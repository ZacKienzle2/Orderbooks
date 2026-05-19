---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0008. Single-threaded engine with SPSC boundary rings

## Context and Problem Statement

The matching engine is the system's hot path. Concurrency inside the
engine would introduce locking, false sharing, and non-deterministic
ordering. Some concurrency is unavoidable at the boundary: the gateway
that feeds orders runs on a different thread, and consumers of fills /
book deltas may run on yet others.

## Decision Drivers

- Strict price-time priority requires deterministic single-threaded
  ordering inside the engine.
- Boundary must allow producer / consumer threads to push and pop
  without blocking the engine.
- Lock-free queues must be appropriate to the producer / consumer
  cardinality; over-engineering wastes cycles.
- Multi-symbol scaling, when added, must compose without refactoring
  the single-symbol engine.

## Considered Options

- Single-threaded engine, single SPSC ring per boundary.
- Multi-producer / single-consumer (MPSC) ingress + SPSC egress.
- Sharded engines, one per CPU core, dispatched by symbol hash.
- Lock-protected single engine.

## Decision Outcome

Chosen option: **Single-threaded engine** pinned to one isolated core,
**Vyukov-style bounded SPSC ring** at the ingress (commands) and the
egress (fills + book deltas). Heads and tails are cache-line padded;
buffer is power-of-2 capacity; pushes and pops are wait-free under
non-full / non-empty.

The engine is templated on `Publisher` and uses no globals, so a shard
router for multi-symbol scaling can compose over the same engine type,
dispatched by `wyhash(symbol_id) & (N-1)`, without touching domain code.

### Consequences

- Positive: Engine logic is deterministic; matching order is the order
  commands arrive on the ring.
- Positive: SPSC ring is the cheapest concurrent queue; pushes / pops
  are loads, stores, and a single fence.
- Positive: Cache-line padding prevents producer / consumer false
  sharing on the head and tail.
- Positive: Sharded multi-symbol layer composes cleanly when needed.
- Negative: Single gateway thread. Multi-gateway setups need MPSC,
  documented in a separate ADR if and when required.
- Negative: SPSC ring has fixed capacity; producers must back off when
  full. Backpressure semantics belong to the gateway layer.

## Pros and Cons of the Options

### Single engine + SPSC boundary

- Pro: Deterministic, easy to reason about.
- Pro: Cheapest possible concurrency primitive.
- Pro: Compatible with shard routing without engine changes.
- Con: One gateway per engine; multi-gateway needs MPSC.

### MPSC ingress + SPSC egress

- Pro: Multiple producers can submit to the same engine.
- Pro: Vyukov MPSC is well-known and lock-free.
- Con: Slower per push than SPSC.
- Con: Adds complexity without current need.

### Sharded engines

- Pro: Higher aggregate throughput via parallelism.
- Pro: Each shard preserves price-time priority within its symbol.
- Con: Building the shard router up front when the current scope is one
  symbol is YAGNI.

### Lock-protected single engine

- Pro: Easiest to implement.
- Con: Lock contention is fatal for sub-microsecond latencies.
- Con: Defeats the purpose of single-threading the engine for
  determinism.

## More Information

- Vyukov, D. "Bounded MPMC queue." 1024cores.net.
- Related: [ADR-0009](0009-crtp-publisher-no-virtual-hot-path.md)
  (Publisher concept that the egress adapter satisfies).
