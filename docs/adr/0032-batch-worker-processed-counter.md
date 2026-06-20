---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0032. Batch the shard worker's processed counter

## Context and Problem Statement

Each shard worker drains its ingress ring, applies every command to its engine,
and increments a per-shard processed counter with a release fetch_add so a
draining producer can observe quiescence. The counter was incremented once per
command, on the matching path, even though it is read only by a producer calling
drain, never by the matching path itself. A release read-modify-write on every
command is overhead the hot loop does not need at that granularity.

## Decision Drivers

- The counter update is on the busiest path in the system, so a per-command cost
  there is paid on every order.
- The counter is only ever read by a draining producer, so it needs to be
  current at drain time, not after each command.
- The drain handshake must keep working, so every engine mutation before the
  observed count must still be visible to the producer.

## Considered Options

- Drain up to a fixed batch of commands, then do one release fetch_add for the
  batch.
- Keep one release fetch_add per command.
- Replace the counter with a relaxed store of an absolute running total per
  command.

## Decision Outcome

Chosen option: **drain up to a batch then do one release fetch_add**, because it
amortises the atomic and its fence over the batch while preserving the drain
handshake.

The worker pops up to a batch of commands, applies each, then adds the batch
count to the processed counter with a single release store. A batch shorter than
the cap, the common case once the ring runs dry, still publishes immediately, so
drain never waits longer than one batch behind. The single release store after
the batch publishes every engine mutation in it to the producer's acquire load,
exactly as the per-command store did, so the happens-before that drain relies on
is unchanged. The stop path drains the remainder and publishes once more.

A single-threaded microbenchmark isolated the per-command cost. A release
fetch_add measured about 20 reference cycles per call; amortised over a batch of
64 it is about 0.3, so roughly 20 cycles leave the worker's per-command cost.
Against an engine command of order 60 to 100 cycles that is a fifth to a quarter
of the worker's per-command overhead. The end-to-end throughput gain is real but
not cleanly measurable on a shared, unpinned host, where scheduler noise across
the producer, workers, and merger dominates; the isolated atomic cost is the
defensible figure, and the runtime's drain and quiescence tests cover
correctness.

### Consequences

- Positive: the busiest loop drops a release read-modify-write from all but one
  in every batch of commands.
- Positive: the drain handshake and its memory ordering are unchanged, so the
  existing runtime tests cover it.
- Positive: behaviour is identical; the same commands apply in the same order.
- Negative: drain can observe the count up to one short batch behind, a
  negligible delay on the cold drain path.
- Negative: the batch size is a fixed constant, not tuned to the engine's
  per-command cost; a future profile on isolated hardware could revisit it.

## Pros and Cons of the Options

### Batch then one release fetch_add

- Pro: amortises the atomic, keeps the drain ordering, behaviour identical.
- Con: drain trails by at most one short batch.

### One fetch_add per command

- Pro: the counter is always exactly current.
- Con: pays a release read-modify-write on every order for a value only drain
  reads.

### Relaxed absolute store per command

- Pro: cheaper than a fetch_add, still per command.
- Con: a relaxed store breaks the release-acquire publication drain depends on to
  see the engine mutations.

## More Information

- Implementation: `include/lob/shard_worker.hpp`, `drive_shard`.
- Tests: `tests/test_shard_egress_runtime.cpp` and `tests/test_shard_runtime.cpp`
  exercise drain and quiescence.
- Related: ADR-0008 (SPSC boundary) and ADR-0019 (threaded shard runtime) for the
  ring and worker this sits on.
