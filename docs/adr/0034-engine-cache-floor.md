---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0034. Engine hot path is at its cache-miss floor

## Context and Problem Statement

After the AoS id_index (ADR-0033), the engine hot paths remain memory-latency
bound (submit near 200 cyc, modifyp near 80 cyc, deep mix near 260 cyc). The
question was whether the remaining D1 misses can be driven lower, and if so by
what. cachegrind on the deep workload, a deterministic per-line cache
simulation that needs no PMU, attributes the D1 read misses and so names the
targets directly.

cachegrind (deep, 200k ops, depth 40k) attributes D1 read misses to the
id_index hash bucket (~31 percent), the intrusive FIFO hook write that links a
new order after the level's prior tail (~17 percent), level and order field
access (~19 percent), and the profiler harness's own record array (~16 percent,
not the engine). Last-level read misses are 0.6 percent of D1 read misses, so
the misses are cheap L1-to-L2 hits near 12 cycles and almost none reach DRAM.

## Decision Drivers

- A perf change ships only on a measured win, the discipline of ADR-0026 and
  ADR-0027.
- The miss rate falls only by touching fewer cache lines, not by hiding latency
  and not by adding bookkeeping that misses on its own.
- The structures are already cache-conscious (dense ladder, slab arena,
  hierarchical bitmap, open-addressed index), so a change must beat a high bar.

## Considered Options

- Software-prefetch the independent misses on the resting submit path.
- Co-locate a level's orders by price-affine arena allocation (per-level pools).
- Accept the engine is at its cache floor and stop chasing the miss rate.

## Decision Outcome

Chosen option: **accept the cache floor**, because two co-location attempts were
built and measured and both failed to lower the miss rate, one of them raising
it.

Submit-path prefetch. Prefetching the destination level slot and the id_index
home bucket at the top of `rest_` left the L1 miss count unchanged, as a
prefetch hides a miss rather than removing it, and the cyc/op deltas were pure
host noise (a cancel path byte-identical between the two binaries reported a 138
percent miss swing run to run). Rejected for the same reason as ADR-0027.

Price-affine allocation. A per-price single-slot free cache, the minimal form of
per-level pooling, parked a freed order for reuse by the next order at the same
price. It was correctness-clean (id_index, snapshot, and torture tests pass
under ASAN and UBSAN with 7976, 2133, and 43310 assertions) but cachegrind, run
deterministically, showed total D1 read misses rise 17 percent and submit rise
37 percent. The per-price cache array is itself a Ticks-sized structure whose
indexed access misses more than the co-location saves, and single-slot affinity
never delivers the contiguity that would warm the FIFO tail. A full chunked
allocator would add yet more per-chunk bookkeeping against the same cheap-miss
target at far higher silent-corruption risk, so it was not built.

### Consequences

- Positive: the engine keeps its proven-fastest form and the one real win, the
  AoS id_index, stands.
- Positive: the two dead ends are recorded with data so they are not re-walked.
- Positive: the remaining misses are confirmed cheap L1-to-L2 hits, so the miss
  rate is not the bottleneck a redesign should target.
- Negative: the intrusive-list tail-write miss (ADR-0006) is left unhidden, an
  inherent cost of the layout that affinity allocation does not address.

## Pros and Cons of the Options

### Submit-path prefetch

- Pro: cheap to try, a hint that cannot break correctness.
- Con: cannot lower the miss rate, and the measured cyc effect was noise.

### Price-affine allocation

- Pro: a real reduction in a level's slot scatter if the bookkeeping were free.
- Con: the per-price cache array adds more misses than it removes, a measured 17
  percent regression.

### Accept the cache floor

- Pro: keeps the fastest form, redirects effort to where headroom remains.
- Con: leaves the inherent intrusive-list miss in place.

## More Information

- Measurement: `apps/profile` and cachegrind, interleaved A/B for cyc/op and
  deterministic cachegrind for the per-op miss count.
- Related: ADR-0006 (intrusive FIFO), ADR-0023 (huge-page arena) and ADR-0016
  (NUMA first-touch) for the host-level levers that act on the same misses,
  ADR-0027 for the earlier prefetch rejection, ADR-0033 for the AoS win.
- Direction: optimisation headroom now sits in throughput, multi-symbol
  sharding, and the gateway rather than the single-engine miss rate.
