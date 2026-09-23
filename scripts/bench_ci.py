"""Confidence intervals for benchmark runs, after Kalibera and Jones (ISMM 2013).

A benchmark comparison answers "how much faster", and that answer needs an
error bound. This reads repeated runs of one or more systems and reports, for
each, the mean with a confidence interval over the top experiment level, and
for each pair a Fieller interval for the ratio of the means, which is the
effect size confidence interval the paper recommends (equations 4 and 5).

Input is one JSON file per run, either a Google Benchmark report or an object
of the form {"system": "name", "values": [..]}. The file name carries the
system when the JSON does not: `<system>-<round>.json`. Google Benchmark
reports contribute one value per benchmark, so a run of several benchmarks is
summarised benchmark by benchmark.

Usage:
    python scripts/bench_ci.py DIR [--metric real_time] [--confidence 0.95]
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any

# Two-sided 95 percent quantiles of Student's t, indexed by degrees of freedom.
# Beyond the table the Cornish-Fisher expansion is within 0.2 percent.
_T95: dict[int, float] = {
    1: 12.706,
    2: 4.303,
    3: 3.182,
    4: 2.776,
    5: 2.571,
    6: 2.447,
    7: 2.365,
    8: 2.306,
    9: 2.262,
    10: 2.228,
    11: 2.201,
    12: 2.179,
    13: 2.160,
    14: 2.145,
    15: 2.131,
    16: 2.120,
    17: 2.110,
    18: 2.101,
    19: 2.093,
    20: 2.086,
    21: 2.080,
    22: 2.074,
    23: 2.069,
    24: 2.064,
    25: 2.060,
    26: 2.056,
    27: 2.052,
    28: 2.048,
    29: 2.045,
    30: 2.042,
}


def t_quantile(nu: int, confidence: float = 0.95) -> float:
    """The two-sided t quantile for nu degrees of freedom."""
    if nu < 1:
        msg = "t_quantile needs at least one degree of freedom"
        raise ValueError(msg)
    z = statistics.NormalDist().inv_cdf(1.0 - (1.0 - confidence) / 2.0)
    if confidence == 0.95 and nu in _T95:
        return _T95[nu]
    return z + (z**3 + z) / (4 * nu)


def interval(values: list[float], confidence: float = 0.95) -> tuple[float, float]:
    """Mean and half-width of the confidence interval for these run means."""
    n = len(values)
    mean = statistics.fmean(values)
    if n < 2:
        return mean, math.inf
    half = t_quantile(n - 1, confidence) * statistics.stdev(values) / math.sqrt(n)
    return mean, half


def ratio_interval(
    base: list[float], new: list[float], confidence: float = 0.95
) -> tuple[float, float, float]:
    """Fieller interval for mean(new) / mean(base): the ratio and its bounds."""
    y, h = interval(base, confidence)
    y_new, h_new = interval(new, confidence)
    denominator = y * y - h * h
    if denominator <= 0:
        return y_new / y, -math.inf, math.inf
    root = (y * y_new) ** 2 - denominator * (y_new * y_new - h_new * h_new)
    if root < 0:
        return y_new / y, -math.inf, math.inf
    spread = math.sqrt(root)
    lo = (y * y_new - spread) / denominator
    hi = (y * y_new + spread) / denominator
    return y_new / y, lo, hi


def read_run(path: Path, metric: str) -> tuple[str, dict[str, float]]:
    """One run's system name and its value per benchmark."""
    payload: Any = json.loads(path.read_text(encoding="utf-8"))
    system = path.stem.split("-")[0]
    if isinstance(payload, dict) and "benchmarks" in payload:
        out: dict[str, float] = {}
        for entry in payload["benchmarks"]:
            if metric in entry:
                out[str(entry["name"]).split("/")[0]] = float(entry[metric])
        return system, out
    if isinstance(payload, dict) and "values" in payload:
        name = str(payload.get("benchmark", "value"))
        return str(payload.get("system", system)), {
            name: statistics.fmean(float(v) for v in payload["values"])
        }
    msg = f"{path}: expected a Google Benchmark report or a values object"
    raise ValueError(msg)


def collect(directory: Path, metric: str) -> dict[str, dict[str, list[float]]]:
    """Every run's value, keyed by benchmark and then by system."""
    table: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    for path in sorted(directory.glob("*.json")):
        system, values = read_run(path, metric)
        for name, value in values.items():
            table[name][system].append(value)
    return table


def report(table: dict[str, dict[str, list[float]]], confidence: float) -> list[str]:
    """One line per benchmark and system, then the ratios against the first system."""
    lines: list[str] = []
    for name, per_system in sorted(table.items()):
        systems = sorted(per_system)
        for system in systems:
            values = per_system[system]
            mean, half = interval(values, confidence)
            pct = 100.0 * half / mean if mean else math.inf
            lines.append(
                f"{name:34s} {system:10s} n={len(values):3d} "
                f"mean {mean:12.3f} +- {half:9.3f} ({pct:4.1f}%)"
            )
        base = systems[0]
        for system in systems[1:]:
            ratio, lo, hi = ratio_interval(per_system[base], per_system[system], confidence)
            verdict = "faster" if hi < 1.0 else "slower" if lo > 1.0 else "not separated"
            lines.append(
                f"{name:34s} {system:10s} / {base:10s} "
                f"ratio {ratio:6.3f} [{lo:6.3f}, {hi:6.3f}] {verdict}"
            )
    return lines


def main() -> None:
    """Print intervals for the runs in a directory."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--metric", default="real_time")
    parser.add_argument("--confidence", type=float, default=0.95)
    args = parser.parse_args()
    table = collect(args.directory, args.metric)
    if not table:
        msg = f"no runs found in {args.directory}"
        raise SystemExit(msg)
    print("\n".join(report(table, args.confidence)))


if __name__ == "__main__":
    main()
