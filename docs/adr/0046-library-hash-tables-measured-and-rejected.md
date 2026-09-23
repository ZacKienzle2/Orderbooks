---
status: "Accepted"
date: "2026-09-23"
deciders: ["Zac Kienzle"]
---

# 0046. Library hash tables measured and rejected for the id index

## Context and Problem Statement

The id index maps a client's order id to the order resting in the arena and is
the most-touched indirection on the by-id path. It is written for this
repository: linear probing over 16-byte key and value slots, a load factor at or
below one half, SplitMix64 as the hash, and backward-shift deletion (ADR-0017,
ADR-0033). A library table is preferred wherever one exists, so
`boost::unordered_flat_map` replaced it and was then measured. This record
settles the question against the published comparison of hashing schemes rather
than against preference.

## Decision Drivers

- Richter, Alvarez and Dittrich compare five schemes and four hash functions
  across load factors and key distributions, and their decision tree names the
  scheme to use for a given workload. This engine's index is read-heavy, holds
  integer keys, and is sized so the load factor never passes one half.
- The hash must survive an adversarial client, since order ids arrive over the
  gateway. Pagh, Pagh and Ruzic show that a pairwise-independent hash can give
  linear probing logarithmic expected cost on a constructed key set, while
  five-wise independence restores constant cost.
- A replacement must not cost time on the engine's hot path.

## Considered Options

- Keep the hand-written linear-probing table with SplitMix64.
- `boost::unordered_flat_map`, an open-addressed table that probes a group of
  fifteen slots with one SIMD compare of their metadata.
- `ankerl::unordered_dense::map`, which reaches a value through a bucket array
  and then a dense array.
- Linear probing with the multiply-shift hash, which Richter et al. name as the
  fastest hash in their study.

## Decision Outcome

Chosen option: **the hand-written linear-probing table with SplitMix64**, which
is what Richter et al.'s decision tree selects for this workload: a load factor
below one half, reads at least as frequent as writes, and a dense key
distribution give `LPMult`, linear probing over an open-addressed table. The
hash stays SplitMix64 rather than their multiply-shift, because multiply-shift
is only pairwise independent and the ids come from clients; Pagh et al. give the
construction that makes such a hash degrade, and SplitMix64's finaliser costs
two multiplies against one.

The measurements agree with the published comparison. Over 15 rounds that
rotated which binary ran first and randomised the environment size each round,
after Mytkowicz et al., the ratio of the library table's mean to the
hand-written table's, with 95 percent Fieller intervals, was:

| Benchmark        | Ratio | Interval       |
| ---------------- | ----: | -------------- |
| deep sweep       | 1.607 | [1.149, 2.195] |
| modify, quantity | 1.538 | [1.095, 2.110] |
| submit latency   | 1.476 | [1.083, 1.988] |
| modify, price    | 1.412 | [1.004, 1.923] |
| match, crossing  | 1.327 | [0.973, 1.816] |
| cancel           | 1.253 | [0.889, 1.706] |
| submit, warm     | 0.817 | [0.558, 1.155] |

Four of those intervals lie above one, so the library table is slower there and
the difference is separated from parity. None lies below one. An earlier run
without layout randomisation suggested the library was faster on warm submits;
with the setup randomised that interval covers one, so it was noise.
`ankerl::unordered_dense` was slower again, by 28 to 44 percent on every
workload but cancel.

### Consequences

- Positive: the by-id engine keeps the faster table, and the handle engine of
  ADR-0042 does not use an index at all.
- Positive: the reserved sentinel id returns with the table, so the wire and
  snapshot validators keep rejecting 2^64 - 1 as before.
- Negative: the table stays code this repository maintains, with its own tests.
- Negative: the choice rests on a 2015 comparison that predates the SIMD-probed
  group layout Boost now uses, so it is the measurements that rule out that
  layout here, not the paper.

## More Information

- Related: ADR-0033 (the layout this keeps), ADR-0042 (handles, which bypass the
  index), ADR-0038 (the batch prefetch the library table could not support,
  since neither library exposes the slot an id hashes to).
- Measurement: `lob_bench` engine benchmarks through `scripts/bench_ci.py`,
  which reports effect size confidence intervals after Kalibera and Jones.

- Richter, S., Alvarez, V., & Dittrich, J. (2015). A seven-dimensional analysis
  of hashing methods and its implications on query processing. _PVLDB_, 9(3).
  <https://doi.org/10.14778/2850583.2850585>
- Pagh, A., Pagh, R., & Ruzic, M. (2007). Linear probing with constant
  independence. _STOC_. <https://doi.org/10.1145/1250790.1250839>
- Kalibera, T., & Jones, R. (2013). Rigorous benchmarking in reasonable time.
  _ISMM_. <https://doi.org/10.1145/2464157.2464160>

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
