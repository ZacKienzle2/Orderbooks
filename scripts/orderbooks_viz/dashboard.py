"""Streamlit dashboard for interactive event-log exploration.

Run: `uv run streamlit run scripts/orderbooks_viz/dashboard.py -- --log artifacts/sim.jsonl`
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import streamlit as st

from orderbooks_viz import bitmap_occupancy, depth, event_log, flow_heatmap, top_series


@st.cache_resource(show_spinner=False)
def _load(log_path: str, mtime_ns: int) -> event_log.EventLog:
    """Streamlit-cached read_file keyed on (path, mtime).

    Uses cache_resource (by-reference) rather than cache_data (by-value):
    EventLog is frozen with slots, so concurrent dashboard sessions can
    safely share one instance. cache_data would pickle the four backing
    DataFrames on every cache check, defeating the point on large logs.
    The mtime_ns key invalidates the cache whenever the on-disk file
    changes so the dashboard still reflects fresh runs.
    """
    del mtime_ns  # cache key only
    return event_log.read_file(log_path)


def _parse_argv() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--log", type=Path, default=None, help="JSON Lines event log emitted by lob_replay"
    )
    parser.add_argument("--bench", type=Path, default=None, help="Google Benchmark JSON output")
    raw = sys.argv[1:]
    if "--" in raw:
        raw = raw[raw.index("--") + 1 :]
    return parser.parse_args(raw)


def main() -> None:
    args = _parse_argv()
    st.set_page_config(page_title="Orderbooks", layout="wide")
    st.title("Orderbooks dashboard")

    log_path = st.sidebar.text_input("Event log path", str(args.log) if args.log else "")
    if not log_path:
        st.info("Provide a JSON Lines event log path in the sidebar to begin.")
        return

    log_p = Path(log_path)
    if not log_p.exists():
        st.error(f"Log not found: {log_path}")
        return
    log = _load(log_path, log_p.stat().st_mtime_ns)
    st.sidebar.markdown(
        f"**fills** {len(log.fills)}  |  **tops** {len(log.tops)}  |  "
        f"**trades** {len(log.trades)}  |  **self-trades** {len(log.self_trades)}"
    )

    tab_top, tab_depth, tab_flow, tab_heatmap = st.tabs([
        "Top-of-book",
        "Depth snapshot",
        "Fill density",
        "Occupancy heatmap",
    ])

    with tab_top:
        st.pyplot(top_series.render(log))

    with tab_depth:
        max_seq = int(log.tops["seq"].max()) if not log.tops.empty else 0
        chosen = st.slider("Sequence", 1, max(1, max_seq), value=max_seq)
        st.pyplot(depth.render(depth.at_seq(log, chosen)))

    with tab_flow:
        if log.fills.empty:
            st.warning("No fill events in the supplied log.")
        else:
            st.pyplot(flow_heatmap.render(log))

    with tab_heatmap:
        st.pyplot(bitmap_occupancy.render(log))


if __name__ == "__main__":
    main()
