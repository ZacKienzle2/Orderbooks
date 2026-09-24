"""Heatmap of populated bid / ask price ticks over the event sequence."""

from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np
from matplotlib.figure import Figure

from .event_log import NoTopEventsError

if TYPE_CHECKING:
    from pathlib import Path

    from .event_log import EventLog


def render(
    log: EventLog,
    *,
    px_bins: int = 80,
    bins: int = 200,
    output: str | Path | None = None,
) -> Figure:
    """Render bid/ask price occupancy over time as a two-row heatmap.

    The event sequence is binned into `bins` columns. Price takes one row per
    tick while the range observed in top events fits in `px_bins` rows, and is
    binned into `px_bins` rows past that, so the figure's size does not grow
    with the tick range. Cells are coloured by the aggregate quantity at the
    (price, time-bin) cell. Two panels (bid above, ask below) so polarity and
    lead/lag are visible at a glance.
    """
    if log.tops.empty:
        raise NoTopEventsError

    tops = log.tops
    seqs = tops["seq"].to_numpy()
    seq_range = (seqs.min(), seqs.max())

    fig = Figure(figsize=(10, 6))
    ax_bid, ax_ask = fig.subplots(2, 1, sharex=True)
    for ax, side, cmap in ((ax_bid, "bid", "Greens"), (ax_ask, "ask", "Reds")):
        # The columns are read as arrays and masked there rather than through a
        # boolean DataFrame index, which copies the frame before histogram2d
        # reads three of its columns back out as arrays anyway.
        qty = tops[f"{side}_qty"].to_numpy()
        keep = qty > 0
        if keep.any():
            px = tops[f"{side}_px"].to_numpy()[keep]
            px_min, px_max = px.min(), px.max()
            h, px_edges, seq_edges = np.histogram2d(
                px,
                seqs[keep],
                bins=[min(px_max - px_min + 1, px_bins), bins],
                range=[(px_min - 0.5, px_max + 0.5), seq_range],
                weights=qty[keep],
            )
            ax.imshow(
                h,
                aspect="auto",
                origin="lower",
                interpolation="nearest",
                extent=(seq_edges[0], seq_edges[-1], px_edges[0], px_edges[-1]),
                cmap=cmap,
            )
        ax.set_ylabel(f"{side} px (ticks)")
    ax_bid.set_title("Top-of-book occupancy and aggregate qty over time")
    ax_ask.set_xlabel("sequence")

    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150, bbox_inches="tight")
    return fig
