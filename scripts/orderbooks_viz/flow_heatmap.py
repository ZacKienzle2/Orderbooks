"""Heatmap of fill activity over (price, time) bins."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

from .event_log import EventLog


def render(
    log: EventLog, *, px_bins: int = 80, time_bins: int = 120, output: str | Path | None = None
) -> plt.Figure:
    """Render fill density across (price, sequence) bins.

    The matching engine's fill stream is the directly observable trade tape;
    bin counts approximate where price discovery is happening across the
    chosen window. Cell colour scales with summed traded quantity, not the
    number of fills.
    """
    if log.fills.empty:
        raise ValueError("event log has no fill events")

    fills = log.fills.copy()
    px_edges = np.linspace(fills["px"].min() - 0.5, fills["px"].max() + 0.5, px_bins + 1)
    seq_edges = np.linspace(fills["seq"].min(), fills["seq"].max(), time_bins + 1)
    h, _, _ = np.histogram2d(
        fills["px"].to_numpy(),
        fills["seq"].to_numpy(),
        bins=[px_edges, seq_edges],
        weights=fills["qty"].to_numpy(),
    )

    fig, ax = plt.subplots(figsize=(10, 5))
    im = ax.imshow(
        h,
        aspect="auto",
        origin="lower",
        interpolation="nearest",
        extent=(seq_edges[0], seq_edges[-1], px_edges[0], px_edges[-1]),
        cmap="viridis",
    )
    fig.colorbar(im, ax=ax, label="traded qty")
    ax.set_xlabel("sequence")
    ax.set_ylabel("price (ticks)")
    ax.set_title("Fill density over (price, time)")
    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150, bbox_inches="tight")
    return fig
