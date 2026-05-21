"""Visualisation and analysis harness for the Orderbooks matching engine.

The package consumes the JSON-Lines event stream emitted by the `lob_replay`
binary (and any other publisher built against `lob::json_recorder`) and
renders publication-quality plots plus an interactive Streamlit dashboard.

Modules:
    event_log: parse and partition a JSON-Lines event stream.
    latency:   render Google Benchmark latency histograms.
    top_series: plot best bid, best ask, and spread over time.
    depth:     reconstruct the level-2 book at a chosen sequence.
    bitmap_occupancy: heatmap of populated price ticks over time.
    flow_heatmap: heatmap of order arrivals over (price, time) bins.
"""

from __future__ import annotations

__all__ = (
    "bitmap_occupancy",
    "depth",
    "event_log",
    "flow_heatmap",
    "latency",
    "top_series",
)
