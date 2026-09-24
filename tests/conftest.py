"""Register the harness types so ``st.from_type`` resolves them.

The event stream draws each field over the width the engine declares for it in
``include/lob/types.hpp``, capped at the int64 columns ``event_log`` documents,
under the keys ``json_recorder`` writes. Sequence numbers are assigned in
order, since the engine emits them strictly increasing.
"""

from io import StringIO
from typing import Any

import numpy as np
import orjson
from hypothesis import strategies as st

from orderbooks.viz.depth import BookSnapshot
from orderbooks.viz.event_log import EventLog, read_text

_INT64 = int(np.iinfo(np.int64).max)
_TICK = st.integers(min_value=0, max_value=int(np.iinfo(np.uint32).max))
_QTY = st.integers(min_value=0, max_value=_INT64)
_ORDER_ID = st.integers(min_value=0, max_value=_INT64)
_ACCOUNT = st.integers(min_value=0, max_value=int(np.iinfo(np.uint32).max))
_SEQ = st.integers(min_value=0, max_value=_INT64)
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
        orjson.dumps({**event, "seq": seq}).decode()
        for seq, event in enumerate(events, start=1)
    )


_STREAMS = st.lists(_EVENTS).map(_jsonl)

st.register_type_strategy(StringIO, _STREAMS.map(StringIO))
st.register_type_strategy(EventLog, _STREAMS.map(read_text))
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
