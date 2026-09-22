---
status: "Accepted"
date: "2026-09-22"
deciders: ["Zac Kienzle"]
---

# 0043. Boost unordered_flat_map for the id index

## Context and Problem Statement

`id_index` maps a client's order id to the order resting in the arena. Since
ADR-0017 it has been a table written for this repository, linear probing over
16-byte slots with backward-shift deletion and a reserved empty key, and
ADR-0033 moved it to one array of key and value pairs. Libraries now provide
open-addressed tables of the same family, and the repository takes a library
wherever one exists. The question is what the library costs on the engine's
paths, and whether the hand-written table is still worth keeping for it.

## Decision Drivers

- A library replaces hand-written code unless it measures materially slower on
  the engine's hot path.
- Every operation stays allocation-free after construction.
- The id-mode engine keeps its semantics, and the handle engine of ADR-0042 does
  not use the index at all.

## Considered Options

- Keep the hand-written table.
- `boost::unordered_flat_map`, from Boost.Unordered, which the build already
  takes Boost from.
- `ankerl::unordered_dense::map`, the table ADR-0007 used in its segmented form.

## Decision Outcome

Chosen option: **`boost::unordered_flat_map`**, reserved for the engine's
capacity at construction, behind the engine's `insert`, `lookup`, `take` and
`erase`. The table probes a group of fifteen slots with one SIMD compare of
their metadata and keeps no reserved key, so the id domain widens to every
nonzero value, and the gateway and snapshot validators accept 2^64 - 1.

Neither library table exposes a prefetch of the slot an id hashes to, so the
first stage of the batch prefetch (ADR-0038) no longer starts an index line. The
second stage still looks the id up and starts the order it names, and the
default distances of two and one still measured best, at a median of 136.1
cycles per op against 139.0 to 155.7 for the six others tried.

Median reference cycles per op, 15 rounds alternating which binary ran first on
one pinned core:

| Workload        | Hand-written | Boost |
| --------------- | -----------: | ----: |
| deep            |        242.7 | 263.8 |
| stream          |        153.2 | 158.3 |
| stream --rename |        172.4 | 162.0 |
| submit          |        171.5 | 182.4 |
| cancel          |         24.1 |  19.0 |
| modifyp         |         97.2 |  97.7 |
| modifyq         |        125.5 | 121.4 |
| cross           |         39.0 |  38.1 |

The table costs between 3 and 9 percent where orders are inserted, since a slot
and its group's metadata are two lines where a hand-written slot was one. It
gains 21 percent on cancels of a deep book and 6 percent on renames, which its
smaller footprint would explain, since it runs at a higher load factor than the
hand-written table's half. `ankerl::unordered_dense` measured 28 to 44 percent
slower than the hand-written table on every workload but cancel, since it
reaches each value through a bucket array and then a separate dense array.

### Consequences

- Positive: 130 lines of hashing, probing and deletion go, with the test of
  their internals, and the reserved id with them.
- Positive: the handle engine, which ADR-0042 measured at 72.8 cycles per op on
  the stream workload, never touches the index, so the fastest path loses
  nothing.
- Negative: submit-heavy flow by id pays up to 9 percent.
- Negative: the table initialises its metadata on the constructing thread, where
  the hand-written one deferred it to the first insert (ADR-0016).

## More Information

- Supersedes: ADR-0033, which superseded ADR-0017 and ADR-0007.
- Related: ADR-0038 (the batch prefetch), ADR-0042 (handles, which bypass the
  index).
- Measurement: `lob_profile` workloads above against the previous commit, and
  the id-mode stream swept over prefetch distances.
- Boost.Unordered. <https://www.boost.org/doc/libs/release/libs/unordered/>

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
