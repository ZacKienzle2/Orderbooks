---
status: "Accepted"
date: "2026-09-22"
deciders: ["Zac Kienzle"]
---

# 0042. Engine-assigned order handles

## Context and Problem Statement

A cancel or modify names its order by the client's id, so the engine first
probes the id index for the slot holding it and then reaches the order. That is
a chain of two dependent last-level cache hits per command, and a cancel-replace
adds a third random index slot for the next id. ADR-0038 overlaps the chain
across a drained batch but cannot shorten it. An exchange already gives each
accepted order an identifier of its own, FIX OrderID(37), which the client
quotes back. If that identifier says where the engine keeps the order, the
lookup and the index both go.

## Decision Drivers

- An engine that looks orders up by id must run exactly as it did, since the
  shard runtime and every existing client use it.
- A stale identifier, from an order that has filled or been cancelled and whose
  slot the arena has handed to another, must name nothing.
- No bytes of an order may be written outside its lifetime, and nothing the
  standard library provides may be written by hand.

## Considered Options

- Keep the id index as the only way to an order.
- A handle of the order's arena slot and that slot's generation, with the
  generation kept in bytes the order reserves inside its own slot.
- The same handle with the generations in an array of their own, chosen at run
  time by an `engine_config` switch.
- The same handle chosen at compile time by an `order_lookup` template
  parameter.

## Decision Outcome

Chosen option: **the handle, chosen at compile time**.
`engine<P, Ticks, MaxOrders, order_lookup::by_handle>` keeps no id index. Each
order that rests returns an `order_handle`, its slot and that slot's generation,
and a cancel or modify carries it. The arena keeps a generation per slot in its
own array, odd while the slot is allocated, bumped on every allocate and
deallocate, so a handle whose slot has been freed or reused carries a generation
the slot no longer has. The id in the command is checked against the order as
well, so a handle quoted with the wrong id also names nothing.
`order_lookup::by_id` is the default and compiles to the engine as it was.

On the stream workload, 21 rounds alternating which ran first on one pinned
core, handles took the median from 128.2 to 72.8 reference cycles per op and the
minimum from 117.7 to 66.0. With every modify a cancel-replace the median fell
from 148.4 to 77.2, since a rename no longer inserts into an index. Cachegrind
over 400,000 ops agrees. Instructions fell from 212,462,052 to 174,943,169, L1
read misses from 2,551,452 to 2,212,406 and last-level misses from 228,717 to
171,375. The batch prefetch still pays, since a handle names two independent
lines, the order and its generation. Without it the median was 91.0.

By id the engine is unchanged. Cachegrind against the previous commit counts
151,536,311 instructions for submit against 151,533,279, 111,921,692 for cancel
against 111,918,302 and 131,868,672 for the deep mix against 131,345,523, with
the same misses to within 0.02 percent. Commands grew from 40 to 48 bytes, which
still fits one 64-byte ring slot. The stream driver, which copies them, counts
4.9 percent more instructions.

The generation inside the order's slot was the first prototype. It needs no
second cache line, but its bytes are written while no order lives there, which
relies on the arena's knowledge of the order's layout, and the array does the
same with a `std::unique_ptr` and costs nothing on the id path. The run-time
switch measured 4 to 6 percent more instructions on every id workload, from the
branches and from returning a handle through each path, and building a handle
for every modify read the cold generation line, so a modify returns the handle
it carried.

### Consequences

- Positive: on a handle engine a cancel or modify reaches its order with no
  lookup, and the id index, whose storage is reserved but first touched only on
  use, is never touched.
- Positive: the choice is a type, so neither kind of engine carries code for the
  other.
- Negative: a handle engine needs every cancel and modify to carry a handle, so
  its gateway maps a client's order to the handle it acknowledged. No gateway
  does yet.
- Negative: restore reissues handles, so that gateway rebuilds its map from the
  restored book.

## More Information

- Related: ADR-0017 and ADR-0033 (the index this removes), ADR-0038 (the batch
  prefetch), ADR-0016 (first touch, which the generation array keeps).
- Measurement: `lob_profile --workload stream` with and without `--handles`, and
  `cachegrind --cache-sim=yes`.

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
