---
status: "Accepted"
date: "2026-09-22"
deciders: ["Zac Kienzle"]
---

# 0045. HdrHistogram_c for latency measurement

## Context and Problem Statement

ADR-0024 wrote an HDR histogram from scratch after Gil Tene's HdrHistogram, the
same bucket layout, the same bounded relative error and the same percentile
walk. HdrHistogram_c is the maintained C implementation of that design, and
vcpkg ships it. The repository takes a library wherever one exists unless it
measures materially slower on the hot path, and no histogram sits on the
engine's hot path. It records in the load harness, the gateway and the latency
benchmark, outside the interval each sample times.

## Decision Drivers

- The reported percentiles keep their precision and the tests their meaning.
- A sample above the configured range stays visible, clamped and counted, as the
  fix in ADR-0024 made it.
- The exact recorded minimum and maximum stay available for the harness's
  report.

## Considered Options

- Keep the histogram written for ADR-0024.
- `hdr_histogram` from HdrHistogram_c, owned by a small RAII class that keeps
  the clamp and the overflow count.

## Decision Outcome

Chosen option: **HdrHistogram_c**. `lob::latency_histogram` now owns a
`hdr_histogram`, closes it with the object and keeps only the policy the library
leaves to its caller, clamping a sample above the range and counting it. `min()`
and `max()` read the library's exact recorded extremes, and the percentiles are
the library's, the highest value equivalent to the bucket they fall in, as
before. The six histogram tests pass unchanged apart from one comment.

On one pinned core a record takes 2.1 to 2.5 ns where the inline one took 0.9 to
1.2, since the library's record is a call into a static archive, and a p99.9
query over a million samples takes 2.3 us where the old walk took 2.5 to 3.1. A
record runs once per timed sample and outside the interval it times, so the
extra nanosecond moves no reported figure.

### Consequences

- Positive: 120 lines of bucket arithmetic go, and the library's log format,
  interval recorders and atomic recording become available to the harnesses.
- Negative: the build takes one more dependency, a compiled C archive with zlib,
  where the old histogram was a header.
- Negative: a record costs about one nanosecond more.

## More Information

- Supersedes: ADR-0024.
- Measurement: `bench_record_fixed`, `bench_record_varied` and
  `bench_value_at_percentile` against the previous commit, two rounds each.
- HdrHistogram_c. <https://github.com/HdrHistogram/HdrHistogram_c>

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
