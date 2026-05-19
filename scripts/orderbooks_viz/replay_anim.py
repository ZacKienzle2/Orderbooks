"""Animated reconstruction of the top-of-book from an event log.

Each frame shows a horizontal bid / ask depth bar plus the spread; the
animation steps through the recorded sequence at a chosen stride. Output
formats: MP4 (requires ffmpeg on PATH) or GIF (requires Pillow).
"""

from __future__ import annotations

from collections.abc import Iterable
from pathlib import Path

import matplotlib.animation as anim
import matplotlib.pyplot as plt

from .event_log import EventLog


def render(
    log: EventLog,
    output: str | Path,
    *,
    stride: int = 1,
    fps: int = 30,
    figsize: tuple[float, float] = (8.0, 3.5),
) -> anim.FuncAnimation:
    """Render a replay animation.

    Each animation step pulls the next top-of-book snapshot from
    `log.tops` (one row per snapshot, sorted by seq). `stride` advances
    multiple snapshots per frame for long event logs. The output file
    extension chooses the codec; .mp4 uses ffmpeg, .gif uses Pillow.
    """
    if log.tops.empty:
        raise ValueError("event log has no top events")

    tops = log.tops.copy().sort_values("seq").reset_index(drop=True)
    frames = list(range(0, len(tops), max(stride, 1)))

    px_min = int(min(tops["bid_px"].min(), tops["ask_px"].min()))
    px_max = int(max(tops["bid_px"].max(), tops["ask_px"].max()))
    qty_max = int(max(tops["bid_qty"].max(), tops["ask_qty"].max()))

    fig, ax = plt.subplots(figsize=figsize)
    ax.set_xlabel("quantity (negative = bid, positive = ask)")
    ax.set_ylabel("price (ticks)")
    ax.grid(True, alpha=0.3)

    def draw_frame(i: int) -> Iterable[plt.Artist]:
        ax.clear()
        ax.set_xlim(-qty_max, qty_max)
        ax.set_ylim(px_min - 1, px_max + 1)
        ax.axvline(0, color="#37474F", linewidth=1)
        ax.grid(True, alpha=0.3)

        row = tops.iloc[i]
        if row["bid_qty"] > 0:
            ax.barh(row["bid_px"], -row["bid_qty"], color="#2E7D32", height=0.8, label="bid")
        if row["ask_qty"] > 0:
            ax.barh(row["ask_px"], row["ask_qty"], color="#C62828", height=0.8, label="ask")
        ax.set_title(f"seq {int(row['seq'])}")
        ax.set_xlabel("quantity (negative = bid, positive = ask)")
        ax.set_ylabel("price (ticks)")
        if row["bid_qty"] > 0 or row["ask_qty"] > 0:
            ax.legend(loc="lower right")
        return ()

    animation = anim.FuncAnimation(
        fig, draw_frame, frames=frames, interval=1000.0 / max(fps, 1), blit=False
    )

    out = Path(output)
    if out.suffix.lower() == ".gif":
        animation.save(str(out), writer=anim.PillowWriter(fps=fps))
    else:
        animation.save(str(out), writer=anim.FFMpegWriter(fps=fps))
    return animation
