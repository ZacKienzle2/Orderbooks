# Visualisation guide

`scripts/orderbooks_viz/` reads the JSON-Lines event stream emitted by the `lob_replay` binary (and any other publisher built against `lob::json_recorder`) and produces static plots plus an interactive dashboard.

## Pipeline

```text
lob_replay --seed 42 --commands 50000 --output artifacts/sim.jsonl
                                       │
                                       ▼
                            orderbooks_viz (Python)
                                       │
        ┌───────────────┬──────────────┴──────────────┬───────────────┐
        ▼               ▼                             ▼               ▼
    top series     depth snapshot                 fill heatmap   occupancy heatmap
```

## Static plots

```bash
uv run python -m scripts.orderbooks_viz.top_series  # not yet a CLI; use the Python API
```

Programmatic use:

```python
from orderbooks_viz import event_log, top_series, depth, bitmap_occupancy, flow_heatmap

log = event_log.read_file("artifacts/sim.jsonl")
top_series.render(log, output="artifacts/figures/top.png")
depth.render(depth.at_seq(log, 10_000), output="artifacts/figures/depth.png")
bitmap_occupancy.render(log, output="artifacts/figures/occupancy.png")
flow_heatmap.render(log, output="artifacts/figures/flow.png")
```

Each renderer returns the `matplotlib.figure.Figure`; passing `output=` saves it at 150 dpi.

## Latency

```python
from orderbooks_viz import latency

df = latency.load("artifacts/bench.json")  # Google Benchmark JSON output
latency.render(df, output="artifacts/figures/latency.png")
```

## Dashboard

```bash
uv run streamlit run scripts/orderbooks_viz/dashboard.py -- --log artifacts/sim.jsonl
```

Tabs: top-of-book series, scrubbable depth snapshot, fill density heatmap, occupancy heatmap.

## Running the smoke tests

```bash
uv run pytest -q tests/harness/test_viz_smoke.py
```

Tests assert the renderers produce a non-empty figure; visual correctness is by human inspection.
