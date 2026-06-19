---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0026. Absolute latency-ceiling gate in CI

## Context and Problem Statement

The project measures per-operation latency with an in-process HDR histogram
(ADR-0024) and reports p50, p99, p99.9, and max from the engine latency
benchmark. CI already runs a relative regression gate that compares a run
against `bench/baseline.json` with google/benchmark's `compare.py`. That gate
has two gaps. It is dormant until a baseline file exists, and the baseline must
be captured on the CI runner to be comparable, so a freshly cloned pipeline
gates nothing. It also measures only relative drift, so a regression that lands
together with a refreshed baseline is absorbed silently, and the headline
sub-microsecond claim is never asserted as an absolute fact.

A credible latency claim needs a check that is active from the first run and
states a hard number, not just a delta against a moving reference.

## Decision Drivers

- The gate must run on every CI build with no baseline and no prior state.
- It must catch a gross algorithmic regression, an O(1) path turned linear,
  which is the failure that matters most and shows as a large absolute jump.
- It must not flake on a shared, virtualised runner where the deep tail and the
  raw maximum are noisy.
- It must reuse the JSON the benchmark already emits, with no new dependency.

## Considered Options

- A self-contained script that reads the run's own benchmark JSON and fails
  when a percentile exceeds a fixed ceiling.
- Seeding and maintaining `bench/baseline.json` on the runner so the existing
  relative gate activates.
- A statistical-process-control gate that tracks a rolling distribution of past
  runs and flags outliers.

## Decision Outcome

Chosen option: **a self-contained absolute ceiling script**, because it is
active from the first run, states the latency claim as a hard number, and adds
no dependency or stored state.

`scripts/check_latency_ceiling.py` reads `artifacts/bench.json`, selects the
median aggregate row of the latency benchmark, and compares its percentile
counters against ceilings supplied on the command line or through environment
variables. The median aggregate is chosen over the mean or the max because it
discards the single noisy spike a shared runner produces. p50 and p99.9 carry
default ceilings with wide headroom over a healthy run, while p99 and the raw
maximum stay unset by default because they are the noisiest signals and the
histogram reports a saturated maximum at its trackable ceiling. Exit codes form
the contract, 0 within ceiling, 1 on a breach, and 2 on a configuration or
input fault, so a renamed benchmark fails loudly rather than disabling the gate.

### Consequences

- Positive: the gate runs on every build with no baseline, so the latency
  claim is enforced continuously rather than asserted in prose.
- Positive: an absolute ceiling catches a regression that lands with a
  refreshed baseline, which the relative gate cannot see.
- Positive: no new dependency and no stored state; the script is plain standard
  library over the existing JSON.
- Negative: a fixed ceiling is coarse, so it catches gross regressions but not
  a small steady creep, which the relative gate still covers.
- Negative: the ceilings are host-relative reference cycles, so a move to a
  materially different runner needs the defaults retuned.

## Pros and Cons of the Options

### Self-contained absolute ceiling

- Pro: active from the first run, no baseline, no dependency, fails loud on
  misconfiguration.
- Con: a fixed number needs retuning if the runner class changes.

### Seed and maintain a baseline

- Pro: reuses the existing relative gate and catches small creep.
- Con: stays dormant until the baseline is captured on the runner, and absorbs
  a regression that lands alongside a baseline refresh.

### Statistical-process-control gate

- Pro: adapts to the runner and flags distributional outliers.
- Con: needs stored history and tuning out of proportion to a single-binary
  project's CI, and still drifts with a slow regression.

## More Information

- Implementation: `scripts/check_latency_ceiling.py`.
- Tests: `tests/harness/test_check_latency_ceiling.py` drives the gate as a
  subprocess over synthetic JSON and asserts the exit-code contract.
- CI wiring: the `Latency ceiling gate` step in `.github/workflows/bench.yml`.
- Related: ADR-0024 (HDR histogram) for the percentiles the gate reads, and
  `docs/dev/bench.md` for the operator view.
