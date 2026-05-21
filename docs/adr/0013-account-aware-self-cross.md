---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0013. Account-aware self-cross detection and enforcement

## Context and Problem Statement

[ADR-0012](0012-self-cross-policy-configurable.md) committed to a
construction-time `self_cross_policy` enum with three behaviours
(`cancel_newest`, `cancel_oldest`, `decrement_trade`) but left the
detection mechanism unspecified. Order ids are unique per order, so the
maker and the aggressor of a same-account self-cross do not share an id;
the engine needs an account-level identifier to compare.

A second event type is required for `decrement_trade`: the policy must
record that quantity was netted between two orders without emitting a
regular `fill_msg` or `trade_msg` (the netting carries no economic
exchange and should not appear on the public tape).

## Decision Drivers

- The detection must cost essentially nothing on the no-self-cross path
  (most fills involve different accounts).
- The account identifier should fit inside the existing 64-byte `order`
  cache line so the per-fill compare stays a single read.
- Downstream risk and compliance consumers need a distinct event for
  netted self-trades.
- A submit with no account (gateway-issued internal flow, replay
  fixtures, tests) should opt out of self-cross dispatch cleanly.

## Considered Options

- Add an `account_id` field to `order` and `submit_msg`; treat
  `account_id == 0` as "no account, skip self-cross check".
- Look up account through a side table keyed by `order_id`.
- Embed account in `order_id` itself via a tagged-pointer scheme.
- Hash the originating gateway / connection id at the engine boundary
  and use that.

## Decision Outcome

Chosen option: **`account_id_t = std::uint32_t` field on `order` and
`submit_msg`**, occupying the slot that was `_pad1` on the order. The
order stays exactly 64 bytes wide. `account_id == 0` is the sentinel
that disables self-cross dispatch; non-zero values are opaque to the
engine and compared with `==`.

A new `self_trade_msg` event is added to the message taxonomy and to
the `event` tagged union. The publisher concept gains a corresponding
`publish(self_trade_msg const&)` requirement, so any sink wired to the
engine is forced to handle the event.

### Self-cross dispatch in the match loop

Before each fill, the engine compares `maker.account_id` to
`m.account_id`. When they match (and neither is zero):

- `cancel_newest`: zero the aggressor's remaining quantity and return
  from the match loop. The resting maker is untouched. No event emitted.
- `cancel_oldest`: unlink the maker from its level, remove it from the
  id index, return its slot to the arena, then re-enter the FIFO loop
  to evaluate the next maker. No event emitted.
- `decrement_trade`: emit a `self_trade_msg` carrying both order ids,
  the shared account, the price, and the netted quantity. Decrement
  both `maker.remaining` and the aggressor's remaining by the trade
  quantity. Release the maker if it zeros out. No `fill_msg` or
  `trade_msg`.

### Consequences

- Positive: Detection is a single 32-bit compare per fill candidate. On
  the no-account path (`account_id == 0`) the compare against zero
  short-circuits the dispatch entirely.
- Positive: The order struct keeps its 64-byte width; no cache-line
  reshuffling.
- Positive: `decrement_trade` produces a distinct, typed event that
  downstream risk can route independently of regular fills.
- Negative: Gateways that previously left the field unset will silently
  get the no-self-cross behaviour. Documented; the harness sets
  `account_id = 0` explicitly to exercise this path.
- Risk: Once the engine ships, repurposing the zero sentinel is a
  breaking change.

## Pros and Cons of the Options

### Field on order + submit_msg

- Pro: O(1) compare on the hot path; no extra cache line touched.
- Pro: Trivially copyable through the SPSC ring.
- Pro: Cheap to test (designated initialisers default the field to zero).
- Con: Schema change; every recorder, snapshot serialiser, and replay
  fixture must be aware.

### Side table keyed by order_id

- Pro: No schema change.
- Con: Extra map lookup per maker on the hot path.
- Con: Side table must stay in lockstep with the id index; another
  failure mode.

### Tagged-pointer order ids

- Pro: No new field.
- Con: Reduces usable id space and complicates id arithmetic.
- Con: Reviewers and downstream consumers must understand the encoding.

### Gateway / connection hash

- Pro: Account boundary is naturally the gateway boundary in many setups.
- Con: Forces a specific routing topology and prevents multi-account
  flow through a single gateway.

## More Information

- Implementation: `include/lob/order.hpp`, `include/lob/messages.hpp`,
  `include/lob/engine.hpp::handle_self_cross_`.
- Tests: `tests/test_engine.cpp` covers each of the three policies plus
  the negative case (different account ids); `tests/test_engine_differential.cpp`
  runs the dense-ladder engine against the reference engine under
  `cancel_newest` and `decrement_trade` over thousands of random commands.
- Related: [ADR-0012](0012-self-cross-policy-configurable.md) sets the
  policy enum; this ADR specifies how the policy reaches a fill.
