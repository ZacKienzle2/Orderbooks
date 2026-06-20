---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0030. End-to-end load harness for system throughput and latency

## Context and Problem Statement

The microbenchmarks time engine operations in isolation, and the latency gate
times a single submit in process. Neither exercises the assembled runtime, where
an order crosses an ingress ring to a shard worker, matches, crosses an egress
ring, and is merged into one stream. A sub-microsecond claim for the system, not
just the engine, needs a measurement over that whole path under load, and the
project had none. The harness must also be runnable by anyone, so it cannot
depend on a market-data feed.

## Decision Drivers

- The measurement must cover the full producer-to-consumer path, not one engine
  call, to back a system-level number.
- Throughput and latency are different questions and must not contaminate each
  other; a latency taken under saturation is a queueing figure, not a processing
  one.
- It must need no external data, so the flow is generated.
- It must reuse the existing runtime seam rather than a parallel mock.

## Considered Options

- A two-phase harness over the real runtime, with a max-rate throughput phase
  and a closed-loop latency phase.
- A single max-rate phase reporting both throughput and the latency histogram.
- An external load tool driving the runtime over a socket.

## Decision Outcome

Chosen option: **a two-phase harness over the real runtime**, because it
measures both questions correctly with no new dependency and reuses the assembled
pipeline.

`apps/loadgen` constructs a `shard_egress_runtime`, an `egress_merger`, and a
sink that records latency. Each iteration submits a resting ask and a crossing
bid on one symbol, so every pair yields a fill whose taker is the bid. The
throughput phase fires every order at max rate and reports orders per second;
the ingress rings saturate, so this is the workers' sustained rate. The latency
phase runs a closed loop with one pair in flight, waiting for each order's fill
to echo before sending the next, so the pipeline stays unsaturated and the
measured time is processing latency, not queueing. The producer parks an rdtsc
stamp keyed by the bid's order id before submitting, and the sink differences the
egress stamp against it to time the order's whole journey. Only the latency-phase
orders are stamped, so the histogram holds only unloaded samples. The flow is
generated and the symbols spread across shards through the runtime's own
SplitMix64 routing, so no market data is required.

### Consequences

- Positive: a system-level throughput and latency figure over the real ingress,
  worker, match, egress, and merge path, not an isolated engine call.
- Positive: throughput and latency are measured under the load each needs, so
  neither misleads.
- Positive: no dependency and no market data; the flow is synthetic.
- Positive: built under every CI preset, so the thread-sanitizer build validates
  the harness's cross-thread stamp table.
- Negative: the closed-loop latency includes a fixed cross-thread wake-up cost
  that a busy-polled production consumer would not pay, so it is an upper bound
  on the engine's own contribution.
- Negative: the deep tail needs an isolated, pinned host to settle; on a shared
  runner the maximum reflects scheduler jitter, not the engine.

## Pros and Cons of the Options

### Two-phase harness over the real runtime

- Pro: correct throughput and latency with no dependency, over the real path.
- Con: two phases and a closed-loop wait add code over a single loop.

### Single max-rate phase for both

- Pro: simplest, one loop.
- Con: the latency taken under saturation is dominated by queueing and overstates
  processing latency by orders of magnitude.

### External socket load tool

- Pro: also exercises the network path.
- Con: needs the gateway that does not exist yet, and a separate tool and
  protocol, out of proportion to a first end-to-end measurement.

## More Information

- Implementation: `apps/loadgen/main.cpp`.
- Operator guide: `docs/dev/loadgen.md`.
- Related: ADR-0019 through ADR-0022 for the runtime, ring, and merge seam the
  harness drives, and ADR-0024 for the histogram it records into.
