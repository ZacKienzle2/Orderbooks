"""Checks for the benchmark interval helpers."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

from bench_ci import collect, interval, ratio_interval, t_quantile


def test_t_quantile_matches_the_table() -> None:
    assert t_quantile(10) == pytest.approx(2.228, abs=1e-3)
    assert t_quantile(60) == pytest.approx(2.000, abs=0.01)


def test_interval_widens_with_spread() -> None:
    tight = interval([100.0, 100.5, 99.5, 100.2, 99.8])
    loose = interval([100.0, 120.0, 80.0, 110.0, 90.0])
    assert tight[0] == pytest.approx(100.0, abs=0.2)
    assert tight[1] < loose[1]


def test_ratio_interval_brackets_a_known_speedup() -> None:
    base = [100.0, 101.0, 99.0, 100.5, 99.5]
    new = [80.0, 80.5, 79.5, 80.2, 79.8]
    ratio, lo, hi = ratio_interval(base, new)
    assert ratio == pytest.approx(0.8, abs=0.01)
    assert lo < 0.8 < hi
    assert hi < 1.0  # the speedup is separated from parity


def test_ratio_interval_does_not_separate_identical_systems() -> None:
    base = [100.0, 104.0, 96.0, 101.0, 99.0]
    new = [100.5, 103.0, 97.0, 100.0, 99.5]
    _, lo, hi = ratio_interval(base, new)
    assert lo < 1.0 < hi


def test_collect_reads_google_benchmark_reports(tmp_path: Path) -> None:
    for i, value in enumerate([10.0, 11.0]):
        report = {"benchmarks": [{"name": "bench_x/manual_time", "real_time": value}]}
        (tmp_path / f"v0-{i}.json").write_text(json.dumps(report), encoding="utf-8")
    (tmp_path / "v1-0.json").write_text(
        json.dumps({"system": "v1", "benchmark": "bench_x", "values": [8.0]}), encoding="utf-8"
    )
    table = collect(tmp_path, "real_time")
    assert table["bench_x"]["v0"] == [10.0, 11.0]
    assert table["bench_x"]["v1"] == [8.0]
