---
status: "Accepted"
date: "2026-09-22"
deciders: ["Zac Kienzle"]
---

# 0039. Publish the processed counter with a release store

## Context and Problem Statement

ADR-0032 batched the shard worker's processed counter, so one release fetch_add
publishes a whole drained batch instead of one per command. Of the alternatives
it weighed, it rejected a relaxed store of the running total, correctly, because
a relaxed store does not order the engine's mutations before the count a
draining producer reads.

That left out the store between the two. The worker is the counter's only
writer, so it can hold the running total in a local and publish it with a
release store. A read-modify-write is needed only where two threads add to one
value, and here one thread writes and others read. The question is whether the
store keeps the drain handshake and whether it costs less.

## Decision Drivers

- The drain handshake must hold: a producer that observes the final count must
  also observe every engine mutation that preceded it.
- The counter is updated on the busiest loop in the system, once per batch, and
  once per command when the ring runs a command at a time.
- The fix should use the memory model's own primitive rather than an argument
  about one architecture.

## Considered Options

- Keep the release fetch_add per batch.
- A release store of the running total per batch.

## Decision Outcome

Chosen option: **a release store of the running total**, because it gives the
drain the same guarantee and costs a plain store.

In the C++ memory model a release store and an acquire load that reads its value
synchronise, so every write the worker made before the store happens before
everything the producer does after the load (Boehm and Adve,
doi:10.1145/1375581.1375591; Batty et al., doi:10.1145/1926385.1926394). That is
the edge ADR-0032 relies on, and it needs a release operation, which a store is.
Atomicity of the increment itself is what the fetch_add buys, and a single
writer has no concurrent increment to lose.

Under x86-TSO a release store compiles to a plain `mov`, since the hardware
already keeps stores in order, while a fetch_add compiles to a locked
instruction, a full barrier that drains the store buffer (Sewell et al.,
x86-TSO, doi:10.1145/1785414.1785443). ADR-0032 measured that read-modify-write
at about 20 reference cycles. In `lob_loadgen` built with clang 20 at -O3, the
`drive_shard` instantiation held two `lock add` instructions before the change,
one per publication site, and holds none after it. On AArch64 the store is
`stlr` against a load-exclusive and store-exclusive loop or an LSE atomic. At a
full batch of 64 the saving is a fraction of a cycle per command, and it grows
to the whole 20 cycles when the ring delivers one command at a time, the
unloaded case the load harness's latency phase measures.

### Consequences

- Positive: the worker publishes its count with a plain store on x86.
- Positive: the drain handshake and its tests are unchanged, since the release
  and acquire pair is the same.
- Negative: the counter must stay single-writer. A second writer would need the
  read-modify-write back.

## Pros and Cons of the Options

### Release fetch_add per batch

- Pro: correct for any number of writers.
- Con: a locked read-modify-write the single writer does not need.

### Release store of the running total

- Pro: the same release and acquire edge at the cost of a store.
- Con: correct only while the worker is the counter's sole writer.

## More Information

- Supersedes ADR-0032, keeping its batching.
- Implementation: `include/lob/shard_worker.hpp`, `drive_shard`.
- Tests: `tests/test_shard_runtime.cpp` and
  `tests/test_shard_egress_runtime.cpp` exercise drain and quiescence, and a
  ThreadSanitizer build checks the handshake.
