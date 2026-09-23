---
status: "Rejected"
date: "2026-09-22"
deciders: ["Zac Kienzle"]
---

# 0041. Lazy deletion in the level FIFO measured and rejected

## Context and Problem Statement

A cancel unlinks its order from the level's intrusive FIFO, which writes the
hooks of both neighbours, two orders allocated at other times and so on other
cache lines. Cachegrind on the deep-mix drain put 69 percent of the engine's L1
write misses on those hooks. Lazy deletion leaves a cancelled order linked,
marked retired, and frees it later, so a cancel writes no neighbour. The
question is whether that saves time.

## Decision Drivers

- A perf change ships with an interleaved A/B on the ADR-0027 set.
- Matching, snapshots and rejects at arena capacity must be unchanged, so
  retired orders are skipped everywhere and reclaimed before the arena refuses
  an order.

## Considered Options

- Keep unlinking on cancel.
- Retire on cancel, freeing retired orders when they reach a level's front, when
  a level holds nothing live, and in a compaction before the arena refuses.

## Decision Outcome

Chosen option: **keep unlinking on cancel**, because lazy deletion measured
slower wherever cancels are frequent.

The prototype passed the differential suite, including its run at arena
capacity, and the stress audits with retired orders allowed in a FIFO. Cycles
per op at the median, 15 interleaved rounds on one pinned core:

| Workload        | Unlink | Retire |
| --------------- | -----: | -----: |
| deep            |  244.1 |  273.9 |
| stream          |  127.1 |  175.8 |
| stream --rename |  151.7 |  204.4 |
| submit          |  172.4 |  279.9 |
| cancel          |   22.7 |   30.6 |
| cross           |   38.2 |   39.1 |
| sweep           |   39.9 |   39.5 |
| modifyp         |  101.5 |   97.5 |

Cachegrind over 400,000 ops shows why. The write misses fell as intended, on
submit from 1,069,038 to 627,227 and on the stream from 1,144,163 to 879,990,
but the read misses rose by more, on submit from 1,811,238 to 2,762,441 and on
the stream from 2,343,213 to 2,690,124, with the instruction count unchanged. A
retired order is read again later, cold, when it is freed, and the next submit
no longer takes the slot the cancel just released, which the slab arena's LIFO
free list would have handed back hot. A write miss drains from the store buffer,
and a read miss stalls the load that needs it, so trading the one for the other
lost time.

### Consequences

- Positive: the unlink path, the arena's hot reuse after a cancel and the FIFO
  invariants stay as they are.
- Negative: the neighbour write misses remain, and removing them needs a
  structure where a cancel frees its slot at once, not a deferred free.

## More Information

- Related: ADR-0034 (the cache floor), ADR-0038 (the prefetch that hides the
  misses this does not remove), ADR-0027 (the same measure-then-revert pattern).
- Measurement: `lob_profile` workloads above and `cachegrind --cache-sim=yes`.
