---
status: "Accepted"
date: "2026-06-19"
deciders: ["Zac Kienzle"]
---

# 0019. Threaded shard runtime with core-pinned workers

## Context and Problem Statement

The shard router (ADR-0015) partitions symbols across independent
per-symbol engines but is single-threaded by design, so a caller still
drives every shard from one thread. The router's own comment defers
threading to the caller, and the affinity helpers in
`include/lob/affinity.hpp` plus the SPSC ring in
`include/lob/spsc_ring.hpp` exist but nothing composes them into a
running executor. Without that executor the multi-symbol design cannot
actually occupy more than one core, so the production latency story of
"one shard per pinned core" is unrealised.

The missing piece is a driver that runs each shard on its own thread,
pins that thread to a core, and feeds it commands without a lock on the
hot path or any change to engine and router internals.

## Decision Drivers

- Parallelism must compose over the existing engine and router without
  modifying either. The engine stays single-threaded and deterministic.
- The per-shard queue must preserve the single-producer,
  single-consumer contract so the existing wait-free ring applies with
  no added synchronisation.
- Per-shard command ordering under the runtime must match the order a
  single-threaded dispatch would apply, so behaviour is reproducible.
- The busy-wait must suit a dedicated isolated core yet return the core
  to other work on a shared development host.
- Lifecycle must drop no submitted command and leak no thread, and it
  must expose a quiescence point so the controller can read book state
  or snapshot a shard mid-run.

## Considered Options

- Threaded runtime with one worker per shard, a dedicated per-shard SPSC
  ingress ring, and core pinning through `pin_this_thread_to_core`.
- A thread pool with work-stealing across shards.
- Caller-managed threads, the status quo documented in the router.
- A single dispatcher thread reading one queue and fanning out to every
  shard by switch.

## Decision Outcome

Chosen option: **one worker thread per shard, each pinned to its own
core, draining a dedicated per-shard SPSC ingress ring**, because it
keeps the wait-free per-shard queue contract intact and preserves the
engine's single-threaded determinism while scaling linearly with cores.

```cpp
template <publisher P, std::size_t Ticks, std::size_t MaxOrders,
          std::size_t NumShards, std::size_t RingCapacity>
class shard_runtime {
    // try_submit/try_cancel/try_modify hash the symbol to a shard and
    // push onto that shard's ring; worker i drains ring i into engine i.
};
```

A single producer hashes each command's symbol with the router's
SplitMix64 mapping and pushes onto the owning shard's ring. Worker `i`
is the only consumer of ring `i` and the only thread that ever touches
engine `i`, so no engine field is shared across threads and the SPSC
ring needs no extra fence. The worker pins itself with
`pin_this_thread_to_core(first_core + i * core_stride)`, names itself
for diagnostics, then busy-waits with `cpu_relax` up to `spin_budget`
iterations before yielding to the scheduler.

Backpressure surfaces through the `try_*` return value. A full ring
returns false and the gateway owns the retry-or-drop decision, matching
the boundary semantics of ADR-0008.

`drain()` blocks the producer until every pushed command has been
processed. Each worker increments a per-shard processed counter with a
release store; `drain()` polls those counters with acquire loads, so
when it observes the final count it has also acquired every engine
mutation that worker made. That gives a per-shard quiescence barrier the
controller can use to read book state safely or take a consistent
snapshot without stopping the workers.

`stop()` requests shutdown with a release store on a stop flag and joins
every worker. A worker that sees the stop flag drains its ring to empty
before exiting, so no command pushed before the stop request is lost.

### Consequences

- Positive: throughput scales with the number of pinned cores while each
  shard keeps strict price-time priority and bit-exact determinism.
- Positive: the engine and router are untouched; the runtime is a pure
  composition layer over the existing primitives.
- Positive: `drain()` is a cheap quiescence barrier for mid-run reads and
  warm-snapshot capture.
- Positive: backpressure is explicit at the producer call site.
- Negative: the publisher is shared across workers, so it must be thread
  safe or partitioned into one egress ring per shard.
- Negative: a single producer feeds the runtime; multi-gateway ingress
  still needs the MPSC variant deferred in ADR-0008.
- Risk: a worker on an unpinned or oversubscribed core busy-waits and
  burns a core; `spin_budget` bounds the spin but host isolation governs
  the latency floor (see `docs/dev/threading.md`).

## Pros and Cons of the Options

### One worker per shard, per-shard SPSC ring

- Pro: preserves the wait-free SPSC contract with no added locking.
- Pro: engine isolation makes the parallelism trivially correct.
- Pro: linear core scaling; deterministic per-shard ordering.
- Con: shared publisher must be thread safe or per-shard.

### Thread pool with work-stealing

- Pro: balances load when shards are unevenly busy.
- Con: a stolen shard is then touched by two threads, breaking engine
  isolation and the SPSC contract; correctness would need locking.
- Con: non-deterministic ordering across steals.

### Caller-managed threads

- Pro: zero new code; maximum flexibility.
- Con: every consumer reinvents pinning, draining, and shutdown, and
  most get the memory ordering wrong.

### Single dispatcher thread

- Pro: simplest threading model.
- Con: serialises across shards and defeats the point of sharding.

## More Information

- Implementation: `include/lob/shard_runtime.hpp`; the spin-loop hint is
  `include/lob/spin.hpp`.
- Tests: `tests/test_shard_runtime.cpp` covers byte-exact equivalence to
  the synchronous router over a 4'000-command randomised stream, a
  crossing pair that matches and drains to empty, producer backpressure
  on a full ring, and repeated start / stop cycles.
- Host tuning: `docs/dev/threading.md`.
- Related: [ADR-0008](0008-single-thread-engine-spsc-boundary.md) (the
  single-thread plus SPSC boundary this runtime drives) and
  [ADR-0015](0015-multi-symbol-shard-router.md) (the router it executes).
