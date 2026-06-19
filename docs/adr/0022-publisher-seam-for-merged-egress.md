---
status: "Accepted"
date: "2026-06-19"
deciders: ["Zac Kienzle"]
---

# 0022. Publisher-concept seam for the merged egress stream

## Context and Problem Statement

The merging egress consumer (ADR-0021) forwards a single, totally ordered
event stream through a `merge_sink`, whose one method is
`on_event(const event&, std::uint64_t)`. The existing event consumers, the
JSON Lines recorder and the ring publishers, instead satisfy the publisher
concept (ADR-0009) with typed `publish` overloads per message. Without a
bridge, every downstream would need a bespoke `merge_sink`, and the recorder
would have to learn the merged-stream interface it has no other reason to
know.

The system needs one small seam that lets any existing publisher consume the
merged stream unchanged.

## Decision Drivers

- The recorder and other publishers already speak the publisher concept and
  should not grow a second interface.
- The merger should depend only on the `merge_sink` concept, not on any
  concrete downstream.
- The adapter must add no allocation and no virtual dispatch on the path.

## Considered Options

- A `publisher_sink` adapter that satisfies `merge_sink` and decodes the
  event union into the matching `publish` overload of a wrapped publisher.
- Teach `json_recorder` (and every other consumer) to implement `merge_sink`
  directly.
- A variant-visitor sink that the merger calls with a callable per kind.

## Decision Outcome

Chosen option: **a `publisher_sink<P>` adapter** that satisfies `merge_sink`
and dispatches each event to the wrapped publisher's typed `publish` overload,
because it reuses the publisher concept as the downstream seam and leaves both
the merger and every publisher untouched.

```cpp
template <publisher P>
class publisher_sink {
    void on_event(const event& e, std::uint64_t) noexcept {
        switch (e.k) { /* e.body.<kind> -> pub_->publish(...) */ }
    }
};
```

The full egress pipeline is then a composition of independent pieces, none of
which knows the others' internals:

```text
shard_egress_runtime -> egress_merger -> publisher_sink -> json_recorder
```

The adapter holds the publisher by non-owning pointer, decodes the tagged
union with a switch, and forwards. There is no allocation and no virtual call.
The merge sequence is accepted and ignored, since the recorder orders by the
write order the merger already imposes.

### Consequences

- Positive: any publisher, the JSON Lines recorder today and a wire publisher
  or analysis hook tomorrow, plugs into the merged stream with no new code.
- Positive: the merger stays coupled only to `merge_sink`, and the publishers
  stay coupled only to the publisher concept.
- Positive: the seam is header-only, allocation-free, and free of virtual
  dispatch.
- Negative: a downstream that genuinely needs the global merge sequence must
  read it from the seam rather than from the event, since the engine events
  carry only a per-shard sequence.

## Pros and Cons of the Options

### publisher_sink adapter

- Pro: reuses the publisher concept; zero changes to merger or publishers.
- Pro: allocation-free, no virtual dispatch.
- Con: one indirection from the event union decode, negligible off the
  matching path.

### Make each consumer a merge_sink

- Pro: no adapter type.
- Con: every publisher grows a second, redundant interface and learns about
  the merged stream.

### Variant-visitor sink

- Pro: flexible per-kind callables.
- Con: heavier call shape than a switch and no reuse of the existing
  publisher overloads.

## More Information

- Implementation: `include/lob/publisher_sink.hpp`.
- Tests: `tests/test_publisher_sink.cpp` covers per-kind forwarding to a
  recording publisher and an end-to-end crossing streamed from the runtime
  through the merger into the JSON Lines recorder.
- Related: [ADR-0021](0021-merging-egress-consumer.md) (the merger it feeds)
  and [ADR-0009](0009-crtp-publisher-no-virtual-hot-path.md) (the publisher
  concept it reuses as the seam).
