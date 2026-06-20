---
status: "Accepted"
date: "2026-06-20"
deciders: ["Zac Kienzle"]
---

# 0031. Binary order-entry gateway over TCP

## Context and Problem Statement

The engine and the assembled runtime are a library with in-process drivers. A
client cannot connect and submit an order; nothing terminates a wire protocol.
That gap keeps the project a library rather than a system. The first network
front end needs to accept orders over a socket, run them through the engine, and
acknowledge each, so the order path is reachable end to end and a client can time
a round trip.

## Decision Drivers

- The first gateway should prove the wire path end to end with the least moving
  parts, not solve every protocol and scaling concern at once.
- A client must be able to confirm correctness and measure a round trip, so the
  protocol needs a definite response per order.
- The latency story must not be muddied by avoidable kernel costs, so the link
  should not pay the scheduler's wake-up on every round trip.

## Considered Options

- A fixed-size binary protocol with one ack per order, over a single engine,
  busy-polled.
- A FIX front end reusing the existing parser.
- Wiring the gateway straight onto the sharded runtime.

## Decision Outcome

Chosen option: **a fixed-size binary protocol, one ack per order, over a single
engine, busy-polled**, because it proves the wire path with a minimal, verifiable
surface and leaves protocol richness and sharding to later increments.

A client sends 32-byte `wire_order` records; the gateway reads each straight off
the socket into the struct, dispatches it as a submit, cancel, or modify, and
writes back a 24-byte `wire_ack` carrying the filled quantity and a status. The
one-ack-per-order shape lets a client run closed loop and both check correctness,
every crossing bid must fill, and time the round trip. The connection sets
`TCP_NODELAY` and busy-polls on non-blocking sockets, so the round trip is not
charged Nagle, delayed-ack, or the scheduler's wake-up. A built-in self-test
listens on an ephemeral loopback port, serves on a thread, and drives the client,
so the path is exercised without an external tool.

The gateway runs one engine, not the sharded runtime, to keep the first wire
increment small; routing egress back to the owning connection across shards is a
later step. The protocol is binary rather than FIX because the parser is decode
only and a binary record needs no encoder on the client, and because a fixed
record is the form a latency-sensitive venue actually accepts.

### Consequences

- Positive: the engine is now a connectable endpoint; a client can submit orders
  and read acknowledgements over TCP.
- Positive: the self-test verifies the wire path on every build and the
  thread-sanitizer build covers its threading.
- Positive: busy-polling removes the wake-up cost, so the residual round trip is
  the host network stack, which a production deployment minimises with isolated
  cores and kernel-bypass.
- Negative: one engine and one connection at a time; many connections and the
  sharded runtime behind the gateway are not yet wired.
- Negative: the binary protocol is host byte order and unauthenticated, fit for a
  trusted link, not the public internet.
- Negative: busy-polling spins a core while a connection is idle, which suits a
  dedicated gateway core but wastes a shared one.

## Pros and Cons of the Options

### Binary, one ack per order, single engine, busy-polled

- Pro: minimal verifiable wire path, correctness and round trip both measurable.
- Con: no sharding, one connection, trusted-link protocol only.

### FIX front end

- Pro: a real, standard protocol, and the parser exists.
- Con: needs a FIX encoder on the client and tag-value parsing on the hot path,
  more surface than a first wire increment needs.

### Straight onto the sharded runtime

- Pro: would scale to many symbols at once.
- Con: needs egress routed back to the owning connection per shard, a routing map
  and lifetime problem out of proportion to proving the path.

## More Information

- Implementation: `apps/gateway/main.cpp`.
- Operator guide: `docs/dev/gateway.md`.
- Related: ADR-0018 (FIX parser) for the alternative protocol, ADR-0030 for the
  in-process latency the gateway's wire tax sits on top of, and the roadmap's
  io_uring item for shrinking that tax.
