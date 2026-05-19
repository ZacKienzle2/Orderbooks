"""Reconstruct the level-2 book at a chosen sequence by replaying events."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt

from .event_log import EventLog


@dataclass(slots=True, frozen=True)
class BookSnapshot:
    """Bid and ask aggregates per price tick at a single moment.

    Reconstructed from the top-of-book event stream alone; level-2 detail
    beyond best price requires the engine to emit per-level snapshots which
    is not currently exposed. This snapshot is therefore best-of-book only
    and approximates depth as a single bar per side.
    """

    seq: int
    bid_px: int
    ask_px: int
    bid_qty: int
    ask_qty: int


def at_seq(log: EventLog, seq: int) -> BookSnapshot:
    """Return the top-of-book snapshot as of the latest top event with seq <= `seq`."""
    if log.tops.empty:
        raise ValueError("event log has no top events")
    candidates = log.tops[log.tops["seq"] <= seq]
    if candidates.empty:
        return BookSnapshot(seq=seq, bid_px=0, ask_px=0, bid_qty=0, ask_qty=0)
    row = candidates.iloc[-1]
    return BookSnapshot(
        seq=int(row["seq"]),
        bid_px=int(row["bid_px"]),
        ask_px=int(row["ask_px"]),
        bid_qty=int(row["bid_qty"]),
        ask_qty=int(row["ask_qty"]),
    )


def render(snapshot: BookSnapshot, output: str | Path | None = None) -> plt.Figure:
    """Render a horizontal bid/ask depth bar.

    Bids on the negative axis, asks on the positive axis. Useful in the
    dashboard as a single-frame view; for animated reconstruction across
    sequence ranges, see `replay_anim`.
    """
    fig, ax = plt.subplots(figsize=(8, 3))
    if snapshot.bid_qty:
        ax.barh(snapshot.bid_px, -snapshot.bid_qty, color="#2E7D32", height=0.8, label="bid")
    if snapshot.ask_qty:
        ax.barh(snapshot.ask_px, snapshot.ask_qty, color="#C62828", height=0.8, label="ask")
    ax.axvline(0, color="#37474F", linewidth=1)
    ax.set_xlabel("quantity")
    ax.set_ylabel("price (ticks)")
    ax.set_title(f"Top-of-book at seq {snapshot.seq}")
    ax.grid(True, alpha=0.3)
    if snapshot.bid_qty or snapshot.ask_qty:
        ax.legend(loc="lower right")
    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=150, bbox_inches="tight")
    return fig
