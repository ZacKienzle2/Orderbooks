---
status: "Accepted"
date: "2026-06-19"
deciders: ["Zac Kienzle"]
---

# 0021. Single-threaded merging egress consumer

## Context and Problem Statement

Per-shard egress rings (ADR-0020) give each shard its own event ring,
which is right for the matching threads but awkward for a downstream
that wants one feed. A recorder, a wire publisher, or a monitor would
otherwise have to know the shard count, poll every ring, and impose its
own ordering. That couples each downstream to the runtime's internals
and reinvents the fan-in every time.

The system needs a component that collects the per-shard event rings
into one ordered stream a single downstream can consume, without
breaking the single-producer, single-consumer contract on any ring.

## Decision Drivers

- Each egress ring is single-producer, single-consumer, so exactly one
  consumer may drain it. The fan-in must be that single consumer.
- The downstream wants one ordered stream, not a per-shard poll loop.
- The merger must not couple to the concrete runtime type so it can
  serve any multi-ring egress source.
- Shutdown must not lose events already published to the rings.

## Considered Options

- A single merger thread that drains every shard ring and forwards each
  event to one sink stamped with a global merge sequence.
- Each downstream polls the per-shard rings itself.
- A shared multi-producer queue that every shard pushes into directly.

## Decision Outcome

Chosen option: **a single merger thread that owns the consumer side of
every egress ring and forwards events to one sink with a gap-free global
sequence**, because it preserves each ring's single-consumer contract
and hands the downstream one ordered stream.

```cpp
template <multi_egress_source Source, merge_sink Sink>
class egress_merger {
    // drains every shard of Source, forwarding each event to Sink with a
    // monotonic merge sequence, on its own optionally pinned thread.
};
```

The merger is templated on two concepts rather than a concrete runtime.
`multi_egress_source` is anything exposing `try_poll(shard, event&)` and
`shard_count()`, which `shard_egress_runtime` already satisfies.
`merge_sink` is anything with `on_event(const event&, std::uint64_t)`.
The merger thread loops over the shards, drains each ring, and stamps
every forwarded event with a counter that increases by one per event, so
the sink observes one totally ordered stream.

The sequence is the merger's own arrival order, not a cross-shard
timestamp. The per-shard engines each carry an independent sequence, so
there is no globally meaningful event time to sort by; a deterministic
single-threaded fan-in order is the strongest total order available
without a shared clock.

On stop the merger makes a final pass over every ring before its thread
exits. The contract is to quiesce the producing runtime first, so the
acquire on the stop flag makes every published event visible and the
final pass drains it. The merger never loses an event that was published
before shutdown.

### Consequences

- Positive: downstreams consume one ordered stream and never touch
  per-shard state.
- Positive: the per-shard rings keep their single-consumer contract; the
  merger is that consumer.
- Positive: concept-based coupling lets the merger serve any multi-ring
  egress source, not just the current runtime.
- Negative: the merger thread is a fan-in point whose throughput bounds
  aggregate egress; a single downstream that cannot keep up backpressures
  through full rings and the per-shard drop counters.
- Risk: the merge sequence is arrival order, not event time, so it must
  not be read as a cross-shard causal order.

## Pros and Cons of the Options

### Single merger thread with global sequence

- Pro: preserves the single-consumer contract on every ring.
- Pro: one ordered stream; downstream stays simple and decoupled.
- Con: one thread bounds aggregate egress throughput.

### Each downstream polls the rings

- Pro: no extra thread.
- Con: every downstream re-implements the fan-in and ordering and binds
  to the shard count.
- Con: two downstreams polling the same ring break the single-consumer
  contract.

### Shared multi-producer queue

- Pro: one queue to read.
- Con: reintroduces multi-producer contention on the publish hot path,
  the exact cost ADR-0020 removes.

## More Information

- Implementation: `include/lob/egress_merger.hpp`.
- Tests: `tests/test_egress_merger.cpp` covers a crossing forwarded with
  a gap-free sequence and exactly-once delivery of every event across
  shards over a multi-symbol stream.
- Related: [ADR-0020](0020-per-shard-egress-rings.md) (the per-shard
  egress rings it fans in) and
  [ADR-0019](0019-threaded-shard-runtime-core-pinned-workers.md) (the
  runtime that produces them).
