"""Best bid, best ask, and spread over the event sequence."""

from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt

from .event_log import EventLog


def render(log: EventLog, output: str | Path | None = None) -> plt.Figure:
    """Render the top-of-book time series.

    The x axis is the event sequence; the y axis is price in ticks. Best bid
    is plotted in one colour, best ask in another, and the spread (ask - bid
    when both exist) is shaded.

    Returns the matplotlib Figure; saves to `output` if a path is given.
    """
    if log.tops.empty:
        raise ValueError("event log has no top events")

    tops = log.tops.sort_values("seq")

    fig, (ax_px, ax_spread) = plt.subplots(
        2, 1, figsize=(10, 6), sharex=True, gridspec_kw={"height_ratios": [3, 1]}
    )

    has_bid = tops["bid_qty"] > 0
    has_ask = tops["ask_qty"] > 0
    bid = tops.where(has_bid)
    ask = tops.where(has_ask)

    ax_px.step(bid["seq"], bid["bid_px"], where="post", color="#2E7D32", label="best bid")
    ax_px.step(ask["seq"], ask["ask_px"], where="post", color="#C62828", label="best ask")
    ax_px.set_ylabel("price (ticks)")
    ax_px.set_title("Top of book over time")
    ax_px.grid(True, alpha=0.3)
    ax_px.legend(loc="upper right")

    spread = (ask["ask_px"] - bid["bid_px"]).where(has_bid & has_ask)
    ax_spread.step(tops["seq"], spread, where="post", color="#37474F")
    ax_spread.fill_between(tops["seq"], 0, spread, step="post", alpha=0.2, color="#37474F")
    ax_spread.set_xlabel("sequence")
    ax_spread.set_ylabel("spread (ticks)")
    ax_spread.grid(True, alpha=0.3)

    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150, bbox_inches="tight")
    return fig
