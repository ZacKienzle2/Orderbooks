---
status: "Accepted"
date: "2026-06-19"
deciders: ["Zac Kienzle"]
---

# 0024. From-scratch HDR histogram for latency measurement

## Context and Problem Statement

A sub-microsecond engine is only credible if its latency distribution is
measured, and the interesting part is the tail: p99, p99.9, p99.99. Reporting
a mean or even a single p99 from a microbenchmark hides the multi-modal shape
that jitter, page faults, and contention produce.

Capturing a distribution needs a histogram that spans the whole range, from a
handful of nanoseconds at the floor to milliseconds in the tail, while keeping
fine resolution near the floor. A flat linear histogram cannot: nanosecond
buckets across a millisecond range is a million buckets, and coarsening them
throws away the floor resolution that matters most. The project already has
nanobench for quick tail snapshots, but it needs an in-process histogram it
can feed from its own timing loop and, later, gate regressions against.

## Decision Drivers

- The histogram must hold both fine resolution near the floor and a wide range
  in bounded memory.
- record() runs inside the timing loop, so it must be O(1) and allocate
  nothing.
- Percentile queries must be exact over the recorded data, not interpolated
  guesses.
- No external dependency, consistent with the single-binary design.

## Considered Options

- A from-scratch HDR histogram in the style of HdrHistogram, with magnitude
  buckets each split into linear sub-buckets.
- A dependency on the HdrHistogram_c library.
- A flat linear histogram with a fixed bucket width.

## Decision Outcome

Chosen option: **a header-only HDR histogram**, because it gives bounded
relative error across the whole range in tens of kilobytes, records in O(1),
and adds no dependency.

The value range is divided into power-of-two magnitude bands, and each band is
split into a fixed number of linear sub-buckets set by the requested
significant figures. Three significant figures bound every reported value to
within 0.1 percent of the true value while the counters total a few tens of
kilobytes. record() derives the bucket from the value's bit width with a
count-leading-zeros and bumps one counter. value_at_percentile walks the
counters once and returns the highest-equivalent value of the bucket the
target rank falls in, so a reported percentile never understates the tail.

### Consequences

- Positive: fine resolution at the floor and millisecond range in bounded
  memory, with relative error capped at the requested precision.
- Positive: O(1) allocation-free record, cheap enough to call on every
  measured operation.
- Positive: exact rank-based percentiles over the recorded data.
- Positive: no dependency; the histogram is a plain header.
- Negative: reported values are quantised to the bucket resolution, so two
  values within the same equivalent range read as equal.
- Negative: single-threaded; a multi-threaded measurement keeps one histogram
  per thread and merges them offline.

## Pros and Cons of the Options

### From-scratch HDR histogram

- Pro: bounded error and range in small memory, O(1) record, no dependency.
- Con: the bucket and sub-bucket index math is intricate and needs tests
  against known distributions to trust.

### HdrHistogram_c dependency

- Pro: a mature, well-tested implementation.
- Con: an external C dependency the single-binary design avoids, with a
  C-style ownership API to wrap.

### Flat linear histogram

- Pro: trivial to implement.
- Con: cannot hold floor resolution and tail range at once without either
  millions of buckets or losing the precision that matters.

## More Information

- Implementation: `include/lob/latency_histogram.hpp`.
- Tests: `tests/test_latency_histogram.cpp` checks percentiles of a uniform
  distribution against their known values, a single-valued distribution,
  clamping above the trackable maximum, and reset.
- Benchmarks: `bench/bench_latency_histogram.cpp` measures record and
  percentile-query throughput.
- Reference: Tene, G. *HdrHistogram*. <https://hdrhistogram.org>
