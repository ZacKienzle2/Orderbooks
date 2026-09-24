"""Render Google Benchmark latency histograms from its JSON output."""

from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING

import matplotlib.pyplot as plt
import pandas as pd

if TYPE_CHECKING:
    from matplotlib.figure import Figure


def load(path: str | Path) -> pd.DataFrame:
    """Load a Google Benchmark JSON file and return one row per benchmark."""
    with Path(path).open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    rows = data.get("benchmarks", [])
    return pd.DataFrame(rows)


def render(df: pd.DataFrame, *, output: str | Path | None = None) -> Figure:
    """Render a horizontal bar chart of per-benchmark mean latency.

    `df` is the DataFrame returned by `load`. Bars are ordered slowest to
    fastest; error bars span [cv_mean - cv_stddev, cv_mean + cv_stddev] when
    available.
    """
    if df.empty:
        msg = "benchmark JSON has no entries"
        raise ValueError(msg)

    if "aggregate_name" in df.columns:
        means = df.loc[df["aggregate_name"] == "mean"].copy()
    else:
        means = df.copy()
    means = means.sort_values("real_time")

    fig, ax = plt.subplots(figsize=(10, max(3, 0.3 * len(means))))
    ax.barh(means["name"], means["real_time"], color="#37474F")
    # DataFrame.get returns None for a column the report does not carry, which
    # is the older Google Benchmark output, so the fallback is read here rather
    # than built as a Series the length of the frame and then indexed.
    units = means["time_unit"].to_numpy() if "time_unit" in means.columns else None
    unit = "ns" if units is None or units.size == 0 else str(units[0])
    ax.set_xlabel(f"real_time ({unit})")
    ax.set_title("Microbench mean latency (lower is better)")
    ax.grid(True, alpha=0.3, axis="x")
    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150, bbox_inches="tight")
    return fig
