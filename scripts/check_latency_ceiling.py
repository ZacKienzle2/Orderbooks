"""Absolute latency-ceiling gate for the engine latency microbenchmark.

The relative regression gate (check_bench_regression.py) compares a run
against a stored baseline and only fires when a baseline exists. This gate is
self-contained. It reads the google/benchmark JSON a run already emits, finds
the latency benchmark, and fails when a reported percentile exceeds a fixed
ceiling. It needs no baseline, so it stays active on every run and catches a
gross algorithmic regression (an O(1) path turned linear) that a relative gate
would miss while the baseline drifts with it.

Usage:
    python3 scripts/check_latency_ceiling.py artifacts/bench.json

The benchmark records p50, p99, p99.9, and max as user counters, in the same
unit as its timestamp (reference cycles on x86, nanoseconds elsewhere). Ceilings
share that unit. Each ceiling may be set on the command line or through the
matching environment variable, so CI tunes the gate without editing the
workflow. A ceiling left unset is not checked.

Environment overrides:
    LATENCY_TARGET          benchmark name to gate (default bench_submit_latency)
    LATENCY_P50_CEILING     ceiling on the p50 counter
    LATENCY_P99_CEILING     ceiling on the p99 counter
    LATENCY_P999_CEILING    ceiling on the p99.9 counter
    LATENCY_MAX_CEILING     ceiling on the max counter
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

# Counter key as emitted by the benchmark, environment override, and default
# ceiling. The p50 and p99.9 defaults carry wide headroom over a healthy run
# so ordinary shared-runner jitter never trips the gate, while a tenfold
# regression still does. p99 and max stay unset by default because the deep
# tail is the noisiest signal on a virtualised runner.
_GATES = (
    ("p50", "LATENCY_P50_CEILING", 600.0),
    ("p99", "LATENCY_P99_CEILING", None),
    ("p99.9", "LATENCY_P999_CEILING", 8000.0),
    ("max", "LATENCY_MAX_CEILING", None),
)


class GateError(Exception):
    """A configuration or input fault that should exit with code 2.

    Raised for a missing or malformed JSON file, an absent benchmark, or an
    absent counter, so main keeps one exit path for every such fault instead
    of a return statement per check.
    """


def _ceiling(env_var: str, default: float | None) -> float | None:
    raw = os.environ.get(env_var)
    if raw is None:
        return default
    try:
        return float(raw)
    except ValueError:
        print(f"{env_var} must be a float; got {raw!r}", file=sys.stderr)
        sys.exit(2)


def _load_entry(path: Path, target: str) -> dict[str, object]:
    """Read the benchmark JSON and return the row to gate on.

    With repetitions the run emits aggregate rows suffixed _mean, _median, and
    _stddev. Prefer the median aggregate because it discards the single noisy
    spike a shared runner produces. Without repetitions there is one plain
    iteration row, so fall back to an exact name match.
    """
    if not path.exists():
        raise GateError(f"bench JSON not found: {path}")
    try:
        raw = json.loads(path.read_text())
    except json.JSONDecodeError as exc:
        raise GateError(f"bench JSON malformed: {exc}") from exc

    benchmarks = raw.get("benchmarks") if isinstance(raw, dict) else None
    if not isinstance(benchmarks, list):
        raise GateError("bench JSON has no 'benchmarks' array")

    median = None
    exact = None
    for entry in benchmarks:
        if not isinstance(entry, dict):
            continue
        name = str(entry.get("name", ""))
        if entry.get("aggregate_name") == "median" and name == f"{target}_median":
            median = entry
        if name == target:
            exact = entry
    selected = median or exact
    if selected is None:
        raise GateError(f"latency benchmark {target!r} not found in JSON")
    return selected


def _evaluate(entry: dict[str, object], ceilings: dict[str, float]) -> list[str]:
    """Return the list of ceiling breaches, printing each percentile checked."""
    violations: list[str] = []
    for key, _env_var, _default in _GATES:
        ceiling = ceilings.get(key)
        if ceiling is None:
            continue
        value = entry.get(key)
        if not isinstance(value, int | float):
            raise GateError(f"counter {key!r} absent from the benchmark row")
        status = "ok" if value <= ceiling else "OVER"
        print(f"  {key:>6} = {float(value):10.1f}  ceiling {ceiling:10.1f}  [{status}]")
        if value > ceiling:
            violations.append(f"{key} {float(value):.1f} > {ceiling:.1f}")
    if not ceilings:
        raise GateError("no ceilings configured; nothing gated")
    return violations


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bench_json", type=Path, help="google/benchmark JSON output")
    parser.add_argument(
        "--target",
        default=os.environ.get("LATENCY_TARGET", "bench_submit_latency"),
        help="benchmark name to gate (default bench_submit_latency)",
    )
    for key, env_var, default in _GATES:
        parser.add_argument(
            f"--{key.replace('.', '')}-ceiling",
            dest=key,
            type=float,
            default=_ceiling(env_var, default),
            help=f"ceiling on the {key} counter",
        )
    args = parser.parse_args(argv)

    ceilings = {key: getattr(args, key) for key, _e, _d in _GATES if getattr(args, key) is not None}

    try:
        entry = _load_entry(args.bench_json, args.target)
        violations = _evaluate(entry, ceilings)
    except GateError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    if violations:
        print(
            f"latency ceiling exceeded for {args.target}: " + "; ".join(violations),
            file=sys.stderr,
        )
        return 1
    print(f"latency within ceiling for {args.target} ({len(ceilings)} percentile(s) checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
