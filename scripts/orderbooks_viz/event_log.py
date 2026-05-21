"""Parse the JSON-Lines event stream into pandas DataFrames per event kind."""

from __future__ import annotations

import json
from collections.abc import Iterable
from dataclasses import dataclass
from operator import itemgetter
from pathlib import Path
from typing import Any, TextIO

import numpy as np
import pandas as pd

try:
    import orjson as _orjson
except ImportError:
    _orjson = None


@dataclass(slots=True, frozen=True)
class EventLog:
    """Partitioned view of an event stream by event kind.

    Attributes:
        fills: maker, taker, px, qty, seq.
        tops: bid_px, ask_px, bid_qty, ask_qty, seq.
        trades: px, qty, seq.
        self_trades: aggressor, resting, account, px, qty, seq.
    """

    fills: pd.DataFrame
    tops: pd.DataFrame
    trades: pd.DataFrame
    self_trades: pd.DataFrame


_FILL_COLS = ("seq", "maker", "taker", "px", "qty")
_TOP_COLS = ("seq", "bid_px", "ask_px", "bid_qty", "ask_qty")
_TRADE_COLS = ("seq", "px", "qty")
_SELF_TRADE_COLS = ("seq", "aggressor", "resting", "account", "px", "qty")


def _loads(line: bytes | str) -> Any:
    if _orjson is not None:
        return _orjson.loads(line)
    return json.loads(line)


def _columnar(rows: list[dict], cols: tuple[str, ...]) -> dict[str, np.ndarray]:
    """Project a list of homogeneous dicts into a column-major dict of int64 arrays.

    Uses np.fromiter to fill each column inside the numpy core loop instead
    of a Python-level per-row, per-column assignment; on large logs the
    Python loop dominated the build phase. Missing keys raise KeyError so
    malformed events fail loudly rather than being silently zero-coerced.
    """
    if not rows:
        return {c: np.empty(0, dtype=np.int64) for c in cols}
    n = len(rows)
    return {c: np.fromiter(map(itemgetter(c), rows), dtype=np.int64, count=n) for c in cols}


def _stream(records: Iterable[str | bytes]) -> Iterable[dict]:
    for raw in records:
        if isinstance(raw, bytes):
            if not raw.strip():
                continue
        elif not raw.strip():
            continue
        yield _loads(raw)


def _split(records: Iterable[dict]) -> EventLog:
    fills: list[dict] = []
    tops: list[dict] = []
    trades: list[dict] = []
    self_trades: list[dict] = []
    dispatch = {
        "fill": fills.append,
        "top": tops.append,
        "trade": trades.append,
        "self_trade": self_trades.append,
    }
    for r in records:
        handler = dispatch.get(r.get("kind"))
        if handler is not None:
            handler(r)
    return EventLog(
        fills=pd.DataFrame(_columnar(fills, _FILL_COLS)),
        tops=pd.DataFrame(_columnar(tops, _TOP_COLS)),
        trades=pd.DataFrame(_columnar(trades, _TRADE_COLS)),
        self_trades=pd.DataFrame(_columnar(self_trades, _SELF_TRADE_COLS)),
    )


def read_file(path: str | Path) -> EventLog:
    """Read a JSON-Lines event file and return partitioned DataFrames."""
    with Path(path).open("rb") as handle:
        return _split(_stream(handle))


def read_text(text: str) -> EventLog:
    """Read a JSON-Lines event blob (e.g. captured from stdout) into DataFrames."""
    return _split(_stream(text.splitlines()))


def read_stream(handle: TextIO) -> EventLog:
    """Read from an open text stream (use for stdin or pipes)."""
    return _split(_stream(handle))
