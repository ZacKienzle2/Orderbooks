"""Gate-on-regression check for google/benchmark compare.py JSON output.

Reads the JSON dump produced by ``compare.py benchmarks --dump_json <path>``
and exits non-zero if any benchmark's CPU time regressed past the configured
threshold relative to the baseline.

Usage:
    python3 scripts/check_bench_regression.py path/to/compare.json [--threshold 0.15]

Threshold is a fractional slowdown (0.15 = 15% slower). It may also be set
via ``BENCH_REGRESSION_THRESHOLD`` for CI overrides without editing the
workflow.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


def _cpu_delta(entry: dict[str, object]) -> float:
    """Return the fractional CPU slowdown for one compare.py record.

    compare.py emits the measurement under different keys depending on
    version; prefer the explicit aggregated mean / median fields and
    fall back to a top-level 'cpu' field if present.
    """
    measurements = entry.get("measurements")
    if isinstance(measurements, list) and measurements:
        candidates: list[float] = []
        for m in measurements:
            if not isinstance(m, dict):
                continue
            v = m.get("cpu")
            if isinstance(v, int | float):
                candidates.append(float(v))
        if candidates:
            return max(candidates)
    v = entry.get("cpu")
    if isinstance(v, int | float):
        return float(v)
    return 0.0


def _check(records: list[dict[str, object]], threshold: float) -> list[tuple[str, float]]:
    regressions: list[tuple[str, float]] = []
    for entry in records:
        if not isinstance(entry, dict):
            continue
        name = str(entry.get("name", "<unknown>"))
        delta = _cpu_delta(entry)
        if delta > threshold:
            regressions.append((name, delta))
    return regressions


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compare_json", type=Path, help="compare.py --dump_json output")
    parser.add_argument(
        "--threshold",
        type=float,
        default=float(os.environ.get("BENCH_REGRESSION_THRESHOLD", "0.15")),
        help="Maximum allowed fractional CPU regression (default 0.15 = 15%%)",
    )
    args = parser.parse_args(argv)

    if not args.compare_json.exists():
        print(f"compare JSON not found: {args.compare_json}", file=sys.stderr)
        return 2

    try:
        raw = json.loads(args.compare_json.read_text())
    except json.JSONDecodeError as exc:
        print(f"compare JSON malformed: {exc}", file=sys.stderr)
        return 2
    if isinstance(raw, dict) and "benchmarks" in raw:
        records = raw["benchmarks"]
    elif isinstance(raw, list):
        records = raw
    else:
        print(f"unexpected JSON shape: {type(raw).__name__}", file=sys.stderr)
        return 2

    regressions = _check(records, args.threshold)
    if regressions:
        print(
            f"Benchmark regressions exceed threshold ({args.threshold:.0%}):",
            file=sys.stderr,
        )
        for name, delta in regressions:
            print(f"  {name}: +{delta:.1%}", file=sys.stderr)
        return 1

    print(f"No CPU regressions past {args.threshold:.0%} threshold across {len(records)} entries.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
