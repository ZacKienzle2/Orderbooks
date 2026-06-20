---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0033. AoS slot layout for the id_index

## Context and Problem Statement

ADR-0017 chose an open-addressed hash table for the id_index and stored it as a
structure of arrays, a key array beside a value array. The premise was that a
key-only structure of arrays scans keys densely during a probe. In practice
every engine operation is a point lookup that needs the value the instant the
key matches, so the structure of arrays pays for a layout it never exploits.

A profiler now exists (`apps/profile`), so the layout is measurable. The
question is whether the key and value belong in one slot or two arrays.

## Decision Drivers

- Each cancel, modify, and resting submit does one id_index point lookup or
  insert, and each needs the value as soon as the key matches.
- A probe that reads the key from one array and the value from another touches
  two cache lines roughly a megabyte apart.
- The change must be internal to the index, leaving its public API untouched.

## Considered Options

- Keep the structure of arrays from ADR-0017.
- Fold each key and value into one 16-byte slot, an array of structures.
- Shrink the value to a 32-bit arena index to pack more slots per line.

## Decision Outcome

Chosen option: **one 16-byte slot per entry, an array of structures**, because
co-locating a key with its value turns the two-line probe into a one-line
probe, which a measurement confirms cuts L1 misses on every index operation.

Interleaved A/B on the profiler (build both binaries, then alternate them inside
one process invocation to cancel host drift, minimum of six pinned runs at depth
40k), reading `perf stat` L1 dcache load misses per op:

- submit  L1 miss/op 7.51 to 6.60, a 12 percent drop.
- modifyp L1 miss/op 7.63 to 6.59, a 14 percent drop.
- deep    L1 miss/op 9.65 to 8.22, a 15 percent drop.

The realistic deep mix runs 3.3 percent faster and modifyp 2.9 percent faster.
cancel takes one extra cycle from the wider slot copy in the backward-shift
erase, negligible against the deep gain since cancel already sits at the compute
floor. The 32-bit-index option was rejected because the order id is genuinely
64-bit, so the key cannot shrink and the slot stays 16 bytes either way; the
array of structures captures the win without the false-match risk a truncated
key would add.

### Consequences

- Positive: every lookup, insert, and erase touches one cache line where it
  touched two, the largest single id_index miss source.
- Positive: backward-shift erase moves one slot where it moved two array halves,
  and insert writes one line where it wrote two.
- Positive: the public API is unchanged, so no caller is affected.
- Negative: cancel costs one cycle for the 16-byte slot copy, accepted because
  the deep mix is the path that matters and it improves.

## Pros and Cons of the Options

### Keep the structure of arrays

- Pro: no change.
- Con: pays a second cache line on every point lookup for a key-scan locality
  the engine never uses.

### One slot per entry (array of structures)

- Pro: one-line probe, measured L1 miss reduction across every operation.
- Con: cancel pays one extra cycle for the wider slot copy.

### 32-bit arena index as the value

- Pro: would pack more slots per line if the key were also narrow.
- Con: the 64-bit key keeps the slot at 16 bytes, and a truncated key risks
  false matches that force an extra dereference to disambiguate.

## More Information

- Supersedes ADR-0017 (the structure-of-arrays layout); the open-addressing,
  SplitMix64 hash, load-factor-0.5 sizing, and backward-shift deletion from
  ADR-0017 are retained unchanged.
- Implementation: `include/lob/id_index.hpp`, `slot` struct and `slots_` array.
- Correctness: the id_index differential against `std::unordered_map` across
  three seeds and the engine torture test pass under ASAN and UBSAN.
- Related: ADR-0026 and ADR-0027 for the measure-before-ship discipline this
  change follows.
