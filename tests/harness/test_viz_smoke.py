"""Smoke tests for the orderbooks_viz renderers.

Each test feeds a small synthetic event log into a renderer and asserts the
renderer produces a non-empty figure (size > 1 KiB when saved). Visualisation
correctness is by human inspection; these tests guard the renderer's plumbing
(parser shape, DataFrame columns, matplotlib API).
"""

from __future__ import annotations

import matplotlib

matplotlib.use("Agg")

import pytest

from orderbooks_viz import (
    bitmap_occupancy,
    depth,
    event_log,
    flow_heatmap,
    replay_anim,
    top_series,
)

SAMPLE_LOG = """\
{"kind":"top","seq":1,"bid_px":0,"ask_px":100,"bid_qty":0,"ask_qty":10}
{"kind":"top","seq":2,"bid_px":98,"ask_px":100,"bid_qty":5,"ask_qty":10}
{"kind":"fill","seq":3,"maker":1,"taker":2,"px":100,"qty":4}
{"kind":"trade","seq":3,"px":100,"qty":4}
{"kind":"top","seq":4,"bid_px":98,"ask_px":100,"bid_qty":5,"ask_qty":6}
{"kind":"fill","seq":5,"maker":1,"taker":3,"px":100,"qty":2}
{"kind":"trade","seq":5,"px":100,"qty":2}
{"kind":"top","seq":6,"bid_px":98,"ask_px":101,"bid_qty":5,"ask_qty":4}
{"kind":"self_trade","seq":7,"aggressor":4,"resting":5,"account":7,"px":99,"qty":2}
"""


@pytest.fixture
def log():
    return event_log.read_text(SAMPLE_LOG)


def test_event_log_partitions_by_kind(log) -> None:
    assert not log.fills.empty
    assert not log.tops.empty
    assert not log.trades.empty
    assert not log.self_trades.empty
    assert set(log.fills.columns) >= {"maker", "taker", "px", "qty", "seq"}
    assert set(log.tops.columns) >= {"bid_px", "ask_px", "bid_qty", "ask_qty", "seq"}


def test_top_series_renders(log, tmp_path) -> None:
    out = tmp_path / "top.png"
    fig = top_series.render(log, output=out)
    assert out.exists()
    assert out.stat().st_size > 1024
    fig.clear()


def test_depth_snapshot_renders(log, tmp_path) -> None:
    snap = depth.at_seq(log, 4)
    assert snap.bid_qty == 5
    assert snap.ask_qty == 6
    out = tmp_path / "depth.png"
    fig = depth.render(snap, output=out)
    assert out.exists()
    assert out.stat().st_size > 1024
    fig.clear()


def test_bitmap_occupancy_renders(log, tmp_path) -> None:
    out = tmp_path / "occ.png"
    fig = bitmap_occupancy.render(log, bins=8, output=out)
    assert out.exists()
    assert out.stat().st_size > 1024
    fig.clear()


def test_flow_heatmap_renders(log, tmp_path) -> None:
    out = tmp_path / "flow.png"
    fig = flow_heatmap.render(log, px_bins=8, time_bins=8, output=out)
    assert out.exists()
    assert out.stat().st_size > 1024
    fig.clear()


def test_replay_anim_renders_gif(log, tmp_path) -> None:
    out = tmp_path / "replay.gif"
    replay_anim.render(log, output=out, stride=1, fps=4)
    assert out.exists()
    assert out.stat().st_size > 1024
