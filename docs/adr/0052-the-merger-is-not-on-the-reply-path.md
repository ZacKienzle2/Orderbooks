---
status: "Accepted"
date: "2026-09-23"
deciders: ["Zac Kienzle"]
---

# 0052. The merger belongs to consumers that need a total order, not to a reply

## Context and Problem Statement

ADR-0021 put a single merging consumer in front of the per-shard egress rings,
so a downstream sees one totally ordered stream across every shard. Everything
that reads events has gone through it since, including a client waiting for the
acknowledgement of the order it just sent.

That costs the client a hop. A shard publishes to its ring, the merger takes it
and forwards it, and the client reads the forwarded copy: two core boundaries
where the event only had to cross one. The merger is also a single thread
serving every shard, so it is the first thing to saturate.

Neither cost had been measured, and the second turned out to be worse than the
first.

## Decision Drivers

- A client waiting for its own acknowledgement needs its own event, in order
  with respect to itself. It does not need the other shards' events ordered
  against it.
- A consumer that does need a total order still needs one.
- Losing events is worse than either.

## Considered Options

- Keep every consumer on the merged stream.
- Let a consumer that does not need a total order drain the per-shard rings
  itself, which the runtime's `poll_batch` already exposes.
- Give the merger more threads, which would end the total order it exists for.

## Decision Outcome

Chosen option: **the merged stream is for consumers that need a total order; a
reply path drains the shard rings directly.**

`lob_loadgen --direct` is the arrangement measured. The client polls each
shard's ring in turn, claiming the same batch the merger claims, and remains a
single consumer per ring, which is the contract `spsc_ring` states.

Twelve runs of each, ABBA ordered so a drift in host load reaches both, four
million orders per run, reported by `scripts/bench_ci.py`:

| Metric, reference cycles |          Merged |        Direct | Ratio                |
| ------------------------ | --------------: | ------------: | -------------------- |
| Round trip p50           |   843.8 +- 40.8 | 626.6 +- 19.4 | 1.347 [1.271, 1.425] |
| Round trip p99           | 18,103 +- 7,640 |  3,087 +- 709 | 5.864 [3.277, 9.103] |

At the host's 5.117 GHz counter the median round trip falls from 164.9 ns to
122.5 ns. The 42 ns saved is one cross-core handoff, which is what taking a hop
out of the path should be worth and is consistent with the ring's measured round
trip.

The tail is the larger finding, and the drop counts say why. The merger lost
events in ten of its twelve runs, between eleven thousand and nine hundred and
seventy-two thousand of them. The direct client lost none in any run. A full
egress ring drops rather than blocks (ADR-0020), so a merger that falls behind
four producing shards is not merely slow: the merged stream is lossy under load,
exactly when a downstream needs it most.

### Consequences

- Positive: a client's reply crosses one core boundary. The median round trip is
  35 per cent lower and the tail nearly six times lower.
- Positive: the direct client applies its own backpressure, because the thread
  that produces is the thread that drains, so it cannot outrun itself into a
  full ring.
- Negative: a direct consumer sees per-shard order only. Anything that needs
  events from different shards ordered against each other still needs the
  merger, and pays both the hop and the loss.
- Negative: draining while producing costs the producer throughput, which is why
  the throughput figures for the two modes are not comparable and are not quoted
  here. The latency figures are closed-loop with one order in flight, where they
  are.
- The shared-memory gateway (ADR-0048) already runs the engine on the thread
  that polls, so it has neither the hop nor the merger. This records why that is
  the right shape rather than an accident of how it was written.

## More Information

- Related: ADR-0020 (per-shard rings, and the drop-rather-than-block policy),
  ADR-0021 (the merger and the total order it provides), ADR-0048 (the
  shared-memory channel).
- Reproduce: `lob_loadgen --pin --orders 4000000` against the same with
  `--direct`, alternating, through `scripts/bench_ci.py`.

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
