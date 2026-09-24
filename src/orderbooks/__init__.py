"""Copyright (c) 2026 Zac Kienzle. All rights reserved.

orderbooks: Header-only C++20 matching engine with strict price-time priority, an O(1) hierarchical-bitmap book, and allocation-free hot paths at a 10 ns median and 30 ns p99 submit latency.
"""

from __future__ import annotations

from ._version import version as __version__

__all__ = ["__version__"]
