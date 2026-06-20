# Order-entry gateway

`lob_gateway` (`apps/gateway`) is a binary order-entry gateway over TCP. A client
sends fixed-size `wire_order` records; the gateway decodes each into an engine
command, runs it, and writes back one `wire_ack` summarising the result. The
protocol is one ack per order, so a client times the ack to measure round-trip
latency. It makes the engine a connectable order-entry endpoint rather than a
library.

## Wire protocol

Host byte order, sufficient for a loopback or same-arch link. A cross-arch
deployment would normalise endianness at the boundary.

```text
wire_order  (32 bytes)  id, qty, px, new_px, op, side, tif, pad
wire_ack    (24 bytes)  id, filled, last_px, status
```

`op` is 0 submit, 1 cancel, 2 modify. `status` is 0 accepted, 1 filled, 2 cancel
or modify processed. `filled` and `last_px` summarise the order's fills.

## Run

Self-test (listens on an ephemeral loopback port, serves on a thread, drives a
client that runs a resting-ask, crossing-bid workload and checks every bid fills):

```bash
cmake --build --preset linux-clang-rel --target lob_gateway --parallel
./build/linux-clang-rel/apps/gateway/lob_gateway --orders 50000
```

Server mode for an external client:

```bash
./build/linux-clang-rel/apps/gateway/lob_gateway --listen 7001
# then connect a client to 127.0.0.1:7001 and stream wire_order records
```

## Latency note

The connection busy-polls with `TCP_NODELAY`, so a round trip is not charged the
scheduler's wake-up cost. The remaining round-trip time is dominated by the host
network stack. On a virtualised host such as WSL2 it is hundreds of
microseconds; on bare metal with isolated, busy-polled cores it is single-digit
microseconds. The engine's own latency is the in-process figure from
`lob_loadgen` (`docs/dev/loadgen.md`), about 360 ns end to end. Shrinking the
wire tax with io_uring and kernel-bypass is the next step on the roadmap.

The self-test's headline result is the correctness line, that every crossing bid
filled, which holds regardless of host timing.
