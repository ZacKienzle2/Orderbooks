---
status: "Accepted"
date: "2026-06-19"
deciders: ["Zac Kienzle"]
---

# 0020. Per-shard egress rings for the threaded runtime

## Context and Problem Statement

The threaded shard runtime (ADR-0019) runs each shard on its own worker
thread but shares one publisher across every worker. Each engine calls
the same publisher on its hot path, so the publisher becomes a point of
cross-thread contention and the caller must make it thread safe. A
mutex on the publish path would reintroduce exactly the lock the engine
design works to avoid, and a lock-free shared sink would still bounce a
cache line between every worker core on every event.

The egress side has the same shape as the ingress side. One engine
produces events and one downstream consumes them, which is the
single-producer, single-consumer pattern the bounded ring already
serves. The runtime should give each shard its own egress path so the
publish step stays contention free and the shared-thread-safety
requirement disappears.

## Decision Drivers

- The publish step is on the hot path and must not contend across cores
  or require a lock.
- The egress boundary is single-producer, single-consumer per shard, so
  it should reuse the existing wait-free ring rather than a new
  primitive.
- The engine must stay unchanged. It already publishes through the
  publisher concept, so the egress sink must satisfy that concept.
- A full egress ring must degrade predictably without blocking the
  matching thread.

## Considered Options

- A private egress ring per shard, each engine publishing through its
  own ring-backed publisher.
- One shared lock-free publisher fanning out to per-consumer rings by
  routing context carried on the event.
- A mutex-guarded shared publisher.

## Decision Outcome

Chosen option: **a private SPSC egress ring per shard, fed by a
per-shard ring_publisher**, because it keeps the publish step wait free
and removes the shared-thread-safety requirement entirely.

`ring_publisher` is a small sink that satisfies the publisher concept
and pushes each event into a bounded `spsc_ring<event, Capacity>`. The
new `shard_egress_runtime` owns one egress ring, one ring_publisher, and
one engine per shard, and constructs engine `i` against publisher `i`.
Worker `i` is the only producer of egress ring `i`, and a single
downstream consumer drains it with `try_poll`, so the ring's
single-producer, single-consumer contract holds with no lock on either
boundary.

A full ring drops the event and increments a per-publisher loss counter
rather than stalling the matching thread. A consumer that must not lose
events sizes its ring to its worst-case burst and drains promptly.

The dispatch hash, the worker loop, the spin-wait policy, and the
processed-count drain barrier are factored into `shard_worker.hpp` and
`hash.hpp` and shared with the original runtime, so the two runtimes do
not duplicate the threading or routing logic.

### Consequences

- Positive: the publish step never contends across cores and needs no
  lock or thread-safe shared sink.
- Positive: the egress boundary reuses the existing wait-free ring, so
  there is no new concurrency primitive to verify.
- Positive: backpressure on egress is explicit through the loss counter,
  mirroring the ingress backpressure on the try_* return value.
- Negative: memory scales with the number of shards times the egress
  ring size, since each shard now carries its own ring.
- Negative: a consumer that wants a single merged event stream must
  drain every shard ring and interleave, rather than reading one queue.
- Risk: an undrained egress ring silently drops events; the loss counter
  surfaces it but a consumer that ignores the counter loses data quietly.

## Pros and Cons of the Options

### Private egress ring per shard

- Pro: wait-free publish, no shared sink, no lock.
- Pro: reuses the existing SPSC ring and publisher concept unchanged.
- Con: memory grows with shard count times ring size.

### Shared lock-free publisher with routing fan-out

- Pro: one object to hand to the runtime.
- Con: the event carries no shard or consumer tag, so the fan-out needs
  a routing table the publish path has to consult on every event.
- Con: the shared object still bounces a cache line across cores.

### Mutex-guarded shared publisher

- Pro: simplest to write.
- Con: a lock on the publish hot path defeats the single-thread engine
  design and its sub-microsecond latency target.

## More Information

- Implementation: `include/lob/ring_publisher.hpp` and
  `include/lob/shard_egress_runtime.hpp`; shared worker and hash logic in
  `include/lob/shard_worker.hpp` and `include/lob/hash.hpp`.
- Tests: `tests/test_shard_egress_runtime.cpp` covers ring_publisher
  serialisation and drop accounting, byte-exact book equivalence to the
  synchronous router over a randomised stream, fill delivery on the
  owning shard's egress ring, and drop accounting on an undrained ring.
- Related: [ADR-0019](0019-threaded-shard-runtime-core-pinned-workers.md)
  (the runtime this extends), [ADR-0008](0008-single-thread-engine-spsc-boundary.md)
  (the SPSC boundary), and [ADR-0009](0009-crtp-publisher-no-virtual-hot-path.md)
  (the publisher concept the sink satisfies).
