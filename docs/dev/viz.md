# Visualisation guide

`src/orderbooks/viz/` reads the JSON-Lines event stream emitted by the
`lob_replay` binary (and any other publisher built against `lob::json_recorder`)
and produces static plots plus an interactive dashboard.

## Pipeline

```text
lob_replay --seed 42 --commands 50000 --output artifacts/sim.jsonl
                                       │
                                       ▼
                            orderbooks.viz (Python)
                                       │
        ┌───────────────┬──────────────┴──────────────┬───────────────┐
        ▼               ▼                             ▼               ▼
    top series     depth snapshot                 fill heatmap   occupancy heatmap
```

## Static plots

```bash
uv run python -m orderbooks.viz.top_series  # not yet a CLI; use the Python API
```

Programmatic use:

```python
from orderbooks.viz import event_log, top_series, depth, bitmap_occupancy, flow_heatmap

log = event_log.read_file("artifacts/sim.jsonl")
top_series.render(log, output="artifacts/figures/top.png")
depth.render(depth.at_seq(log, 10_000), output="artifacts/figures/depth.png")
bitmap_occupancy.render(log, output="artifacts/figures/occupancy.png")
flow_heatmap.render(log, output="artifacts/figures/flow.png")
```

Each renderer returns the `matplotlib.figure.Figure`; passing `output=` saves it
at 150 dpi.

## Dashboard

```bash
uv run streamlit run src/orderbooks/viz/dashboard.py -- --log artifacts/sim.jsonl
```

Tabs: top-of-book series, scrubbable depth snapshot, fill density heatmap,
occupancy heatmap.

## Running the tests

```bash
uv run pytest -q
```

The Hypothesis ghostwriter writes the tests, which draw event logs over the
engine's field widths and assert each renderer runs without an exception or a
warning. Visual correctness is by human inspection.
