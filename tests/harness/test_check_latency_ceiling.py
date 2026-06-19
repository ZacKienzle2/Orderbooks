"""Tests for the absolute latency-ceiling gate (scripts/check_latency_ceiling.py).

The gate is exercised as a subprocess against synthetic google/benchmark JSON,
which both proves the command-line contract and matches how CI invokes it. The
exit codes are the contract: 0 within ceiling, 1 on a breach, 2 on a
configuration or input fault.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "check_latency_ceiling.py"

# A healthy run, well under the default ceilings (p50 600, p99.9 8000).
HEALTHY = {"p50": 47.0, "p99": 141.0, "p99.9": 235.0, "max": 1900.0}


def _bench_json(
    tmp_path: Path,
    counters: dict[str, float],
    *,
    name: str = "bench_submit_latency",
    aggregate: bool = True,
) -> Path:
    """Write a minimal benchmark JSON file and return its path.

    When aggregate is set the row carries the _median suffix and the median
    aggregate marker the gate prefers; otherwise it is a single plain iteration
    row the gate falls back to.
    """
    row: dict[str, object] = {"name": f"{name}_median" if aggregate else name}
    if aggregate:
        row["aggregate_name"] = "median"
    row.update(counters)
    path = tmp_path / "bench.json"
    path.write_text(json.dumps({"benchmarks": [row]}))
    return path


def _run(
    path: Path, *args: str, env: dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(SCRIPT), str(path), *args],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )


def test_within_ceiling_passes(tmp_path: Path) -> None:
    result = _run(_bench_json(tmp_path, HEALTHY))
    assert result.returncode == 0, result.stderr


def test_p50_breach_fails(tmp_path: Path) -> None:
    result = _run(_bench_json(tmp_path, HEALTHY), "--p50-ceiling", "10")
    assert result.returncode == 1
    assert "exceeded" in result.stderr


def test_p999_breach_fails(tmp_path: Path) -> None:
    result = _run(_bench_json(tmp_path, HEALTHY), "--p999-ceiling", "100")
    assert result.returncode == 1


def test_env_override_breach_fails(tmp_path: Path) -> None:
    env = {**os.environ, "LATENCY_P50_CEILING": "10"}
    result = _run(_bench_json(tmp_path, HEALTHY), env=env)
    assert result.returncode == 1


def test_missing_file_is_config_fault(tmp_path: Path) -> None:
    result = _run(tmp_path / "absent.json")
    assert result.returncode == 2


def test_unknown_target_is_config_fault(tmp_path: Path) -> None:
    result = _run(_bench_json(tmp_path, HEALTHY), "--target", "no_such_bench")
    assert result.returncode == 2


def test_absent_counter_is_config_fault(tmp_path: Path) -> None:
    result = _run(_bench_json(tmp_path, {"p50": 47.0}))
    assert result.returncode == 2


def test_exact_row_fallback_passes(tmp_path: Path) -> None:
    result = _run(_bench_json(tmp_path, HEALTHY, aggregate=False))
    assert result.returncode == 0, result.stderr
