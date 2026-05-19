"""Parse the JSON-Lines event stream into pandas DataFrames per event kind."""

from __future__ import annotations

import json
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
from typing import TextIO

import pandas as pd


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


def _stream(records: Iterable[str]) -> Iterable[dict]:
    for raw in records:
        line = raw.strip()
        if not line:
            continue
        yield json.loads(line)


def _split(records: Iterable[dict]) -> EventLog:
    by_kind: dict[str, list[dict]] = {"fill": [], "top": [], "trade": [], "self_trade": []}
    for r in records:
        kind = r.get("kind")
        if kind in by_kind:
            by_kind[kind].append(r)
    return EventLog(
        fills=pd.DataFrame(by_kind["fill"]),
        tops=pd.DataFrame(by_kind["top"]),
        trades=pd.DataFrame(by_kind["trade"]),
        self_trades=pd.DataFrame(by_kind["self_trade"]),
    )


def read_file(path: str | Path) -> EventLog:
    """Read a JSON-Lines event file and return partitioned DataFrames."""
    with Path(path).open("r", encoding="utf-8") as handle:
        return _split(_stream(handle))


def read_text(text: str) -> EventLog:
    """Read a JSON-Lines event blob (e.g. captured from stdout) into DataFrames."""
    return _split(_stream(text.splitlines()))


def read_stream(handle: TextIO) -> EventLog:
    """Read from an open text stream (use for stdin or pipes)."""
    return _split(_stream(handle))
