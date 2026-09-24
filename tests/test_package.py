from __future__ import annotations

import importlib.metadata

import orderbooks as m


def test_version() -> None:
    assert importlib.metadata.version("orderbooks") == m.__version__
