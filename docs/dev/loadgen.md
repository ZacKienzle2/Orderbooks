# Load generator

`lob_loadgen` (`apps/loadgen`) drives the assembled multi-shard runtime end to
end under synthetic order flow and reports throughput and end-to-end latency. It
needs no market data; the flow is generated, and the symbols spread across
shards through the same SplitMix64 routing the runtime uses.

## What it measures

Each iteration submits a resting ask and a crossing bid on one symbol, so every
pair produces a fill whose taker is the bid. The full path under test is

```text
producer -> ingress SPSC ring -> shard worker -> match -> egress SPSC ring -> merger -> sink
```

The run has two phases so each number means what it says.

- **Throughput** fires every order at max rate and reports orders per second.
  The ingress rings saturate, so this is the sustained processing rate of the
  workers, not a latency figure.
- **Latency** runs a closed loop with one pair in flight, waiting for each
  order's fill to echo back before sending the next. The pipeline stays
  unsaturated, so the stamp-to-echo difference is processing latency, not
  queueing delay. The producer parks an rdtsc stamp keyed by the bid's order id
  before submitting; the sink differences the egress stamp against it. Only
  these orders are stamped, so the histogram holds only unloaded samples.

## Run

```bash
cmake --build --preset linux-clang-rel --target lob_loadgen --parallel
./build/linux-clang-rel/apps/loadgen/lob_loadgen --orders 20000000 --pin
```

```text
--orders N   total orders for the throughput phase (default 20000000)
--pin        pin shard workers to cores (production tuning)
```

Latency is reported in reference cycles; divide by the host's nominal frequency
for nanoseconds.

## Reading the output

The p50 and p99 are the credible figures. The p99.9 and the maximum are
dominated by scheduler jitter on a shared, unpinned host and only settle on an
isolated machine. For production-quality runs apply the same host tuning as the
microbenchmarks (`docs/dev/bench.md`): isolated cores, performance governor,
turbo off, and `--pin`.
