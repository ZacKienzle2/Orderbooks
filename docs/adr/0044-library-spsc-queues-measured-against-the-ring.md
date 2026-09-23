---
status: "Accepted"
date: "2026-09-22"
deciders: ["Zac Kienzle"]
---

# 0044. Library SPSC queues measured against the ring

## Context and Problem Statement

`spsc_ring` carries every command from the router to a shard and every event
back, so an order crosses it at least twice. It is written for this repository,
and ADR-0040 gave it a sequence per slot. Several libraries ship bounded
single-producer single-consumer queues, and the repository takes a library
wherever one exists unless it measures materially slower on the hot path. The
question is whether any of them carries a command as fast.

## Decision Drivers

- The one-way latency of a handoff, since each order pays it on every boundary.
- The rate a shard drains commands at, since the worker claims up to 64 at a
  time and prefetches across them (ADR-0038).
- A library queue that hands out one element at a time is drained into a local
  batch, so the worker still sees a claim it can look ahead in.

## Considered Options

- Keep `spsc_ring`.
- `boost::lockfree::spsc_queue`, from the Boost the build already takes.
- `rigtorp::SPSCQueue`.
- `atomic_queue::AtomicQueue2` in its SPSC mode.
- `moodycamel::ReaderWriterQueue`.

## Decision Outcome

Chosen option: **keep `spsc_ring`**, because every library measured slower on
both patterns, by more than the spread between runs.

A Google Benchmark comparison carried `lob::command` through each queue with the
two threads pinned to separate cores, seven repetitions per queue, in two runs.
The round trip sends one command out on one queue and back on another. The
stream pushes commands as fast as the consumer drains them in claims of up
to 64. Medians:

| Queue                           | Round trip, ns | Stream, M/s |
| ------------------------------- | -------------: | ----------: |
| `spsc_ring`                     |      124 / 106 |    169 / 80 |
| `boost::lockfree::spsc_queue`   |      186 / 153 |     87 / 55 |
| `rigtorp::SPSCQueue`            |      207 / 165 |     81 / 49 |
| `atomic_queue::AtomicQueue2`    |      170 / 185 |     30 / 28 |
| `moodycamel::ReaderWriterQueue` |      153 / 175 |     75 / 56 |

The second run shared the host with another process, which moved the stream
figures of every queue together. The libraries publish through a head or tail
index the other side reads, as the ring did before ADR-0040, so a handoff moves
the index line and then the slot line, and the ring moves one. atomic_queue
carries its sequence per slot as the ring does, but in a separate array from the
payload, and reads the other side's index to test for room, so each handoff
still moves at least two lines.

### Consequences

- Positive: the handoff keeps its one line, and the worker keeps claiming in
  place without copying commands out.
- Negative: the ring stays code this repository maintains, with its own tests
  and its ThreadSanitizer run.

## More Information

- Related: ADR-0040 (the per-slot sequence), ADR-0038 (the batch claim the
  worker prefetches across), ADR-0043 (the same test applied to the id index).
- Measurement: a disposable Google Benchmark harness over the five queues, the
  same round trip as `bench_round_trip` and a batched stream, with the ports
  from vcpkg.

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
