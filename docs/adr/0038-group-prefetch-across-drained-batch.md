---
status: "Accepted"
date: "2026-09-22"
deciders: ["Zac Kienzle"]
---

# 0038. Prefetch across the commands of a drained batch

## Context and Problem Statement

ADR-0034 records that a single command on the resting path is bound by a short
chain of dependent cache misses, the id_index slot, then the order it names,
then the level and FIFO neighbours an unlink or relink writes. Cachegrind on the
deep mix put 31 percent of the engine's L1 read misses on the id_index and 69
percent of its L1 write misses on the intrusive FIFO hooks, and fewer than one
percent of the L1 misses reached DRAM, so each is a last-level hit of about
fifty cycles. ADR-0027 measured a prefetch issued inside one command and
reverted it, because a hint placed one step before its load has no work to hide
behind.

The shard worker does not see one command at a time. It claims up to 64 commands
from its ingress ring and applies them in order, so while one runs, the ids and
prices of the commands behind it are already in hand. The question is whether
that batch can be used to overlap the misses of later commands with the work of
earlier ones.

## Decision Drivers

- A perf change ships with an interleaved A/B on a representative input, the
  standard ADR-0027 set.
- The prefetch must not change what any command does, so it reads the message
  and the index and writes nothing.
- The best distance depends on the host's cache latency, so it has to be
  settable without a rebuild.

## Considered Options

- Keep applying the batch one command at a time.
- Prefetch the index slot of the command a fixed distance ahead.
- Two stages, the index slot further ahead and the order the slot names nearer.
- Three stages, adding the order's level and FIFO neighbours nearest.

## Decision Outcome

Chosen option: **two stages**, because it measured fastest on the drain of the
deep mix and the third stage lost ground.

This is the group prefetching Chen, Ailamaki, Gibbons and Mowry apply to hash
joins (doi:10.1145/1272743.1272747), with the drained batch as the group. Each
stage of a chain of dependent accesses is issued for a later element while the
current one runs, so the chain's latencies overlap across elements instead of
adding up within one. Asynchronous memory access chaining (Kocberber, Falsafi
and Grot, doi:10.14778/2856318.2856321) and interleaving with coroutines
(Psaropoulos et al., doi:10.14778/3149193.3149202; Jonathan et al.,
doi:10.14778/3236187.3236216) generalise the same idea to irregular chains.

`engine::prefetch` issues the id_index slot and, for a submit, the level at its
price. `engine::prefetch_order` reads the slot, now cached, and issues the order
it names, or for a submit the tail order it will link behind. `apply_batch` in
`shard_worker.hpp` runs the first stage `index_ahead` commands ahead and the
second `order_ahead` commands ahead of each command it applies, and
`drive_shard` drains every claim through it. `spsc_ring::consume_claim` hands
the claim over with indexed access, which the one-element `consume_batch`
callback could not.

The measurement is the `stream` workload of `lob_profile`, the deep mix drained
a batch at a time through the same `apply_batch`, pinned to one core, every
configuration run once per round and the rounds interleaved.

Cycles per deep-mix op, 21 rounds for the 64-command drain and 11 for the
others:

| Plan (index, order) | Batch 64 min | Batch 64 median | Batch 16 median | Batch 1 median |
| ------------------- | -----------: | --------------: | --------------: | -------------: |
| Off                 |        155.7 |           166.5 |           162.8 |          209.7 |
| 2, 0                |        134.5 |           146.4 |                 |                |
| 2, 1                |        125.2 |           132.4 |           137.8 |          213.5 |
| 3, 1                |        125.8 |           137.1 |                 |                |
| 4, 1                |        129.5 |           139.5 |                 |                |
| 4, 2                |        124.9 |           137.2 |           146.4 |                |

The default is two and one, a fifth off the median of a full drain. An earlier
sweep of fifteen rounds put distances of eight and sixteen behind four, and a
third stage behind every two-stage plan it was paired with, eight, four and two
at 179 cycles against 159 for eight and four. A batch of one has nothing to look
ahead to, and its two figures differ by less than their run-to-run spread. The
misses being hidden are last-level hits of about fifty cycles on this host, a
Zen 5 part with 96 MiB of L3, so a short distance covers them. A host whose
working set spills to DRAM wants longer ones, which is why the plan is a field
of shard_runtime_config rather than a constant.

### Consequences

- Positive: the drain of a full batch runs a fifth faster on the deep mix with
  no change to matching semantics.
- Positive: `shard_runtime_config::prefetch` makes the distances a setting, so a
  host whose misses go to DRAM can lengthen them.
- Negative: a batch of one gains nothing, so a lightly loaded shard, which
  drains a command or two at a time, sees the unhidden latency as before.
- Negative: the hints read the index twice per command, which a cache-resident
  workload pays without benefit.

## Pros and Cons of the Options

### One command at a time

- Pro: no code.
- Con: leaves the dependent misses serialised, the cost ADR-0034 measured.

### Index slot only

- Pro: one hint, reading only the message.
- Con: covers the first miss of the chain and leaves the order to miss.

### Two stages

- Pro: covers the two misses every cancel and modify pays.
- Con: the second stage reads the index slot, a second probe per command.

### Three stages

- Pro: reaches the level and FIFO neighbours an unlink writes.
- Con: measured slower. Reading the order to find its neighbours stalls on the
  line the second stage has only just requested.

## More Information

- Related: ADR-0027 (the one-step prefetch this differs from), ADR-0034 (the
  cache floor it overlaps rather than lowers), ADR-0032 (the batch drain it runs
  within).
- Implementation: `include/lob/engine.hpp` (`prefetch`, `prefetch_order`),
  `include/lob/shard_worker.hpp` (`prefetch_plan`, `apply_batch`,
  `drive_shard`), `include/lob/spsc_ring.hpp` (`consume_claim`).
- Measurement: `lob_profile --workload stream --ahead N --ahead2 M --batch B`.
- Verification: `tests/test_engine_differential.cpp` drains random streams
  through `apply_batch` at three plans and three batch sizes and compares every
  event and the final book with the reference engine.
