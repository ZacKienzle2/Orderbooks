---
status: "Rejected"
date: "2026-09-23"
deciders: ["Zac Kienzle"]
---

# 0047. Structural-index FIX parse measured and rejected

## Context and Problem Statement

The gateway's FIX parser walks each `tag=value<SOH>` field byte by byte, and at
about 1,850 instructions per message it is the largest single cost on the
order-entry path outside the engine. Langdale and Lemire parse JSON at gigabytes
per second by splitting the work in two: a branchless first stage turns 64-byte
blocks into bitmaps of the structural characters, and the second stage steps
between them with a count-trailing-zeros instead of testing every byte. FIX is a
flatter grammar than JSON, needing no quote or escape handling, so the technique
should transfer directly.

## Decision Drivers

- The parse must keep its validation, since the gateway is a trust boundary.
- Nothing may read past the caller's buffer, which simdjson handles by requiring
  padded input and this parser cannot require.
- A change ships only with an effect size confidence interval.

## Considered Options

- Keep the byte-at-a-time field scan.
- Build the SOH and `=` bitmaps per 64-byte block with AVX2, walk the fields
  with count-trailing-zeros, and parse tags and values from the resulting spans.

## Decision Outcome

Chosen option: **keep the byte-at-a-time scan**, because the structural index
measured slower on messages of the size this gateway carries.

The prototype built both masks per block with `_mm256_cmpeq_epi8` and
`_mm256_movemask_epi8`, cached the current block, scanned a zero-padded copy for
a trailing partial block so no read passed the caller's buffer, and kept every
validation the byte scan performs. The full suite passed. Over 15 rounds with
the environment size randomised each round, against the previous commit:

| Benchmark        | Ratio | Interval       |
| ---------------- | ----: | -------------- |
| parse, cancel    | 1.424 | [1.314, 1.538] |
| parse, new order | 1.256 | [1.174, 1.343] |

Both intervals lie above one, so the index is slower and the difference is
separated. Restoring the inline digit fold for tags, which the prototype had
replaced with `std::from_chars`, did not change the verdict.

The reason is the premise rather than the implementation. simdjson's first stage
pays a fixed setup per block and wins by amortising it over documents of many
kilobytes. An order-entry message is 80 to 120 bytes, which is two blocks, one
of them partial, and each field spans six to twelve bytes, so the byte scan that
the index replaces was already short, while the index adds mask construction, a
cached-block test per lookup and a padded copy for the tail.

### Consequences

- Positive: the parser keeps its single forward pass and its validation.
- Negative: the parse stays at about 1,850 instructions, and its remaining
  headroom is in value conversion rather than field scanning.
- The technique is worth revisiting only if the gateway ever parses batched
  streams of many messages per buffer, where one index could cover a whole read
  rather than a single message.

## More Information

- Related: ADR-0018 (the parser), ADR-0027 and ADR-0041 (the same
  measure-then-reject pattern).
- Measurement: `bench_parse_new_order_single` and `bench_parse_cancel` through
  `scripts/bench_ci.py`.

- Langdale, G., & Lemire, D. (2019). Parsing gigabytes of JSON per second. _The
  VLDB Journal_, 28. <https://doi.org/10.1007/s00778-019-00578-5>
- Kalibera, T., & Jones, R. (2013). Rigorous benchmarking in reasonable time.
  _ISMM_. <https://doi.org/10.1145/2464157.2464160>
- Mytkowicz, T., Diwan, A., Hauswirth, M., & Sweeney, P. F. (2009). Producing
  wrong data without doing anything obviously wrong! _ASPLOS_.
  <https://doi.org/10.1145/1508244.1508275>

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
