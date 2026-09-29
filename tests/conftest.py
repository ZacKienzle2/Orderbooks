"""Register the harness types so ``st.from_type`` resolves them.

The event stream draws each field over the width the engine declares for it in
``include/lob/types.hpp``, under the keys ``json_recorder`` writes. Sequence
numbers are assigned in order, since the engine emits them strictly increasing.
"""

import json
import tempfile
from pathlib import Path
from typing import Any

import numpy as np
from hypothesis import strategies as st

from orderbooks.viz.depth import BookSnapshot
from orderbooks.viz.event_log import EventLog, read_file

_UINT64 = int(np.iinfo(np.uint64).max)
_TICK = st.integers(min_value=0, max_value=int(np.iinfo(np.uint32).max))
_QTY = st.integers(min_value=0, max_value=_UINT64)
_ORDER_ID = st.integers(min_value=0, max_value=_UINT64)
_ACCOUNT = st.integers(min_value=0, max_value=int(np.iinfo(np.uint32).max))
_SEQ = st.integers(min_value=0, max_value=_UINT64)
_REASON = st.integers(min_value=0, max_value=int(np.iinfo(np.uint8).max))

_EVENTS = st.one_of(
    st.fixed_dictionaries(
        {
            "kind": st.just("fill"),
            "maker": _ORDER_ID,
            "taker": _ORDER_ID,
            "px": _TICK,
            "qty": _QTY,
        }
    ),
    st.fixed_dictionaries(
        {
            "kind": st.just("top"),
            "bid_px": _TICK,
            "ask_px": _TICK,
            "bid_qty": _QTY,
            "ask_qty": _QTY,
        }
    ),
    st.fixed_dictionaries({"kind": st.just("trade"), "px": _TICK, "qty": _QTY}),
    st.fixed_dictionaries(
        {
            "kind": st.just("self_trade"),
            "aggressor": _ORDER_ID,
            "resting": _ORDER_ID,
            "account": _ACCOUNT,
            "px": _TICK,
            "qty": _QTY,
        }
    ),
    st.fixed_dictionaries(
        {
            "kind": st.just("reject"),
            "id": _ORDER_ID,
            "account": _ACCOUNT,
            "px": _TICK,
            "qty": _QTY,
            "reason": _REASON,
        }
    ),
)


def _jsonl(events: list[dict[str, Any]]) -> str:
    """Return the events as the JSON Lines stream ``json_recorder`` writes.

    Args:
        events: Event records without their sequence numbers.

    Returns:
        One JSON object per line, numbered from 1 in order.
    """
    return "\n".join(
        json.dumps({**event, "seq": seq}) for seq, event in enumerate(events, start=1)
    )


def _read(text: str) -> EventLog:
    """Read a stream through a file, the way ``read_file`` reads a log on disk.

    Args:
        text: A JSON Lines event stream.

    Returns:
        The stream partitioned by event kind.
    """
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "events.jsonl"
        path.write_text(text, encoding="utf-8")
        return read_file(path)


st.register_type_strategy(EventLog, st.lists(_EVENTS).map(_jsonl).map(_read))
st.register_type_strategy(
    BookSnapshot,
    st.builds(
        BookSnapshot,
        seq=_SEQ,
        bid_px=_TICK,
        ask_px=_TICK,
        bid_qty=_QTY,
        ask_qty=_QTY,
    ),
)
