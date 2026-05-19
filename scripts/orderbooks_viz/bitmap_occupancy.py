"""Heatmap of populated bid / ask price ticks over the event sequence."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from .event_log import EventLog


def render(log: EventLog, *, bins: int = 200, output: str | Path | None = None) -> plt.Figure:
    """Render bid/ask price occupancy over time as a two-row heatmap.

    The event sequence is binned into `bins` columns; price is binned into
    its full ticks range observed in top events. Cells are coloured by the
    aggregate quantity at the (price, time-bin) cell. Two panels (bid above,
    ask below) so polarity and lead/lag are visible at a glance.
    """
    if log.tops.empty:
        raise ValueError("event log has no top events")

    tops = log.tops.copy()
    seq_min, seq_max = int(tops["seq"].min()), int(tops["seq"].max())
    seq_edges = np.linspace(seq_min, seq_max, bins + 1)

    def panel(side_px: str, side_qty: str) -> tuple[np.ndarray, int, int]:
        active = tops[tops[side_qty] > 0]
        if active.empty:
            return np.zeros((1, bins)), 0, 0
        px_min, px_max = int(active[side_px].min()), int(active[side_px].max())
        px_edges = np.arange(px_min, px_max + 2)
        h, _, _ = np.histogram2d(
            active[side_px].to_numpy(),
            active["seq"].to_numpy(),
            bins=[px_edges, seq_edges],
            weights=active[side_qty].to_numpy(),
        )
        return h, px_min, px_max

    bid_h, bid_lo, bid_hi = panel("bid_px", "bid_qty")
    ask_h, ask_lo, ask_hi = panel("ask_px", "ask_qty")

    fig, (ax_bid, ax_ask) = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
    if bid_h.size:
        ax_bid.imshow(
            bid_h, aspect="auto", origin="lower", interpolation="nearest",
            extent=(seq_edges[0], seq_edges[-1], bid_lo, bid_hi + 1), cmap="Greens",
        )
    ax_bid.set_ylabel("bid px (ticks)")
    ax_bid.set_title("Top-of-book occupancy and aggregate qty over time")

    if ask_h.size:
        ax_ask.imshow(
            ask_h, aspect="auto", origin="lower", interpolation="nearest",
            extent=(seq_edges[0], seq_edges[-1], ask_lo, ask_hi + 1), cmap="Reds",
        )
    ax_ask.set_ylabel("ask px (ticks)")
    ax_ask.set_xlabel("sequence")

    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150, bbox_inches="tight")
    return fig
