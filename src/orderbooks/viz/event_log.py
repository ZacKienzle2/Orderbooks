"""Read the JSON-Lines event stream into pandas DataFrames per event kind."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

import duckdb

if TYPE_CHECKING:
    from pathlib import Path

    import pandas as pd


class NoTopEventsError(ValueError):
    """The log holds no top-of-book events to read."""

    def __init__(self) -> None:
        """State which kind of event is missing."""
        super().__init__("event log has no top events")


class NoFillEventsError(ValueError):
    """The log holds no fill events to read."""

    def __init__(self) -> None:
        """State which kind of event is missing."""
        super().__init__("event log has no fill events")


class MissingFieldError(ValueError):
    """An event lacks a field its kind requires."""

    def __init__(self, kind: str) -> None:
        """Name the kind of event with the missing field."""
        super().__init__(f"a {kind} event is missing a field")


@dataclass(slots=True, frozen=True)
class EventLog:
    """Partitioned view of an event stream by event kind.

    Sequence numbers, order ids and quantities are uint64 columns, the
    std::uint64_t the engine writes them as. Prices, accounts and reject
    reasons are narrower unsigned types in the engine and int64 columns here,
    which hold them whole and subtract without wrapping.

    Attributes:
        fills: maker, taker, px, qty, seq.
        tops: bid_px, ask_px, bid_qty, ask_qty, seq.
        trades: px, qty, seq.
        self_trades: aggressor, resting, account, px, qty, seq.
        rejects: id, account, px, qty, reason, seq.
    """

    fills: pd.DataFrame
    tops: pd.DataFrame
    trades: pd.DataFrame
    self_trades: pd.DataFrame
    rejects: pd.DataFrame


# Each kind json_recorder writes, the EventLog field it fills and its columns.
_KINDS = {
    "fill": ("fills", ("seq", "maker", "taker", "px", "qty")),
    "top": ("tops", ("seq", "bid_px", "ask_px", "bid_qty", "ask_qty")),
    "trade": ("trades", ("seq", "px", "qty")),
    "self_trade": (
        "self_trades",
        ("seq", "aggressor", "resting", "account", "px", "qty"),
    ),
    "reject": ("rejects", ("seq", "id", "account", "px", "qty", "reason")),
}
_UINT64_COLS = frozenset(
    {"seq", "maker", "taker", "aggressor", "resting", "id", "qty", "bid_qty", "ask_qty"}
)
_COLUMNS = {"kind": "VARCHAR"} | {
    c: "UBIGINT" if c in _UINT64_COLS else "BIGINT"
    for _, cols in _KINDS.values()
    for c in cols
}


def _partition(rel: duckdb.DuckDBPyRelation, kind: str) -> pd.DataFrame:
    """One kind's events, raising when an event lacks one of its fields."""
    _, cols = _KINDS[kind]
    frame = rel.filter(f"kind = '{kind}'").select(*cols).df()
    if frame.isna().to_numpy().any():
        raise MissingFieldError(kind)
    return frame


def read_file(path: str | Path) -> EventLog:
    """Read a JSON-Lines event file and return partitioned DataFrames."""
    rel = duckdb.read_json(str(path), format="newline_delimited", columns=_COLUMNS)
    return EventLog(
        **{field: _partition(rel, kind) for kind, (field, _) in _KINDS.items()}
    )
