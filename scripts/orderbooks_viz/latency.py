"""Render Google Benchmark latency histograms from its JSON output."""

from __future__ import annotations

import json
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


def load(path: str | Path) -> pd.DataFrame:
    """Load a Google Benchmark JSON file and return one row per benchmark."""
    with Path(path).open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    rows = data.get("benchmarks", [])
    return pd.DataFrame(rows)


def render(df: pd.DataFrame, *, output: str | Path | None = None) -> plt.Figure:
    """Render a horizontal bar chart of per-benchmark mean latency.

    `df` is the DataFrame returned by `load`. Bars are ordered slowest to
    fastest; error bars span [cv_mean - cv_stddev, cv_mean + cv_stddev] when
    available.
    """
    if df.empty:
        raise ValueError("benchmark JSON has no entries")

    if "aggregate_name" in df.columns:
        means = df[df["aggregate_name"] != ""].copy()
        means = means[means["aggregate_name"] == "mean"]
    else:
        means = df.copy()
    means = means.sort_values("real_time")

    fig, ax = plt.subplots(figsize=(10, max(3, 0.3 * len(means))))
    ax.barh(means["name"], means["real_time"], color="#37474F")
    unit = "ns" if means.empty else means.get("time_unit", pd.Series(["ns"] * len(means))).iloc[0]
    ax.set_xlabel(f"real_time ({unit})")
    ax.set_title("Microbench mean latency (lower is better)")
    ax.grid(True, alpha=0.3, axis="x")
    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150, bbox_inches="tight")
    return fig
