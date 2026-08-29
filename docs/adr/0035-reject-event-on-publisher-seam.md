---
status: "Accepted"
date: "2026-08-29"
deciders: ["Zac Kienzle"]
---

# 0035. Arena exhaustion publishes a reject event

## Context and Problem Statement

`engine::rest_` returned silently when the slab arena could not hold another
resting order. The residual quantity vanished with no event, the gateway acked
the order as accepted while the book held none of it, and the differential
suite could not observe the loss because the reference engine never exhausts
and the crossing test mixes never filled the arena. Downstream risk had no
signal that submitted quantity was gone.

## Decision Drivers

- Every unit of submitted quantity must be accounted for by events (fill,
  self-trade net, cancel, resting, or an explicit loss).
- The hot path must not pay for the exhaustion case; the arena-full branch is
  already the unlikely path.
- The gateway ack must never claim state the book does not hold.

## Considered Options

- Publish an explicit `reject_msg` through the publisher seam.
- Leave rest_ silent and let the gateway infer losses from arena occupancy.
- Grow the arena dynamically so exhaustion cannot happen.

## Decision Outcome

Chosen option: **publish `reject_msg`**. The event carries id, account, px,
the residual quantity lost, a reason, and a seq stamp, and joins the event
union, the publisher concept, the ring and JSON publishers, and the merge-sink
decode. The gateway folds it into a rejected ack. The reference engine mirrors
the behaviour behind a matching `MaxOrders` cap so a non-crossing differential
case that saturates the arena requires both engines to reject identically, and
the GTC conservation invariant accounts rejected quantity explicitly.

Gateway-side inference was rejected because it reconstructs per-order facts
the engine already knows, racily and per-deployment. Dynamic growth was
rejected because the fixed slab is the basis of the arena's locality and
huge-page design (ADR-0016, ADR-0023).

### Consequences

- Positive: quantity conservation is now a checkable identity end to end.
- Positive: the gateway ack is truthful under overload.
- Negative: one more event kind for every publisher implementation to carry.
