---
status: "Accepted"
date: "2026-06-11"
deciders: ["Zac Kienzle"]
---

# 0018. Zero-copy FIX 4.4 parser for the order-entry path

## Context and Problem Statement

The engine consumes `lob::command` values (submit / cancel / modify). The
roadmap calls for a FIX 4.4 ingress so the matching engine can be driven from
a standard order-entry session rather than synthetic commands. The first cut
needs the order-entry subset only: NewOrderSingle, OrderCancelRequest, and
OrderCancelReplaceRequest.

The decoder sits in front of the hot path. It runs once per inbound wire
message and must not allocate, throw, or copy the payload: any per-message
heap traffic on the ingress thread shows up directly as tail latency and as
GC-style jitter that the arena and bitmap work was meant to remove. It also
has to frame a TCP byte stream, where a `read()` may deliver a partial
message or several messages back to back.

## Decision Drivers

- No allocation and no exceptions on the decode path. Fields are views into
  the caller's buffer; numeric conversion is `std::from_chars`.
- The decoder must not depend on the engine template. It speaks `command`,
  the same trivially-copyable tagged union the SPSC ring already transports,
  so the gateway can validate bytes without instantiating a book.
- Stream framing must be explicit. The caller needs to know how many bytes a
  message consumed and when the buffer holds only a prefix.
- The wire envelope must be validated: BeginString, BodyLength, and CheckSum.
  A bad checksum or truncated frame must be a typed error, not undefined
  behaviour or a partial command.
- Header-only and free of platform conditionals, consistent with the rest of
  `include/lob`.

## Considered Options

- **Hand-rolled zero-copy tag-value scanner emitting `lob::command`**: split
  on SOH and `=`, views over the buffer, `from_chars` for numerics, validate
  the 8/9/10 envelope, map MsgType to a command.
- **Vendored FIX engine (QuickFIX or similar)**: full session layer, message
  dictionaries, validation.
- **Materialise a `std::map<int, std::string>` of fields per message, then
  build the command**: simplest to write.

## Decision Outcome

Chosen option: **hand-rolled zero-copy tag-value scanner emitting
`lob::command`**, because it meets the no-allocation / no-exception contract,
keeps the decoder decoupled from the engine template, and gives the gateway
explicit framing without pulling a session-layer dependency into the build.

`parse(std::span<const std::byte>)` returns a `result { error, command,
consumed }`. It reads BeginString(8) and BodyLength(9) to fix the frame,
bounds-checks the buffer against `body_start + body_length + 7` (the CheckSum
field is always `10=` plus three digits plus SOH), validates the trailing
checksum as the sum of the preceding bytes modulo 256, then scans the body
fields between the BodyLength SOH and the CheckSum field. The body view is
truncated to the checksum offset so field scanning cannot stray into the
trailer. MsgType(35) leads the body and selects the command:

- `D` NewOrderSingle -> `submit_msg`, with ClOrdID(11) as the order id,
  Side(54), OrderQty(38), Price(44), TimeInForce(59, default GTC), and
  Account(1) when present.
- `F` OrderCancelRequest -> `cancel_msg`, keyed on OrigClOrdID(41), the
  resting order the engine cancels.
- `G` OrderCancelReplaceRequest -> `modify_msg`, keyed on OrigClOrdID(41)
  with the new Price(44) and OrderQty(38).

Price(44) is taken as an integer tick. The engine's price domain is the dense
`[0, Ticks)` ladder, so the gateway passes ticks and the decode path avoids a
decimal-to-tick float conversion. A decimal-quoting venue converts upstream,
where the contract specifications live.

A partial buffer returns `error::incomplete` with `consumed == 0`; the caller
retries when more bytes arrive. A complete message returns `error::ok` and
`consumed` equal to the full frame length, so a caller draining a stream
advances by `consumed` and re-enters `parse` for the next message.

The session layer (Logon, Heartbeat, sequence numbers, resend, gap fill) is
out of scope. This ADR covers the wire decode of the order-entry subset only;
the gateway adapter that pushes parsed commands into the SPSC command ring and
the libFuzzer harness for malformed inputs remain on the roadmap.

### Consequences

- Positive: decode allocates nothing and cannot throw; the ingress thread has
  no per-message heap traffic.
- Positive: the decoder is independent of `engine<P, Ticks, MaxOrders>`. It is
  unit-tested against `command` alone (`tests/test_fix_parser.cpp`).
- Positive: explicit framing via `consumed` / `error::incomplete` makes TCP
  stream reassembly a caller concern with a clear contract.
- Positive: envelope validation (begin string, body length, checksum) rejects
  malformed and truncated frames as typed errors.
- Negative: only the D / F / G subset is decoded; other MsgTypes return
  `error::unsupported_msg_type` and must be added explicitly.
- Negative: prices are ticks, not decimals; a decimal venue needs an upstream
  conversion shim.
- Risk: a hand-rolled checksum or frame-length calculation off-by-one would
  accept a bad frame. Mitigated by tests covering corrupted checksum, wrong begin
  string, truncated buffer, missing required field, non-numeric value, and a
  concatenated two-message stream.

## Pros and Cons of the Options

### Hand-rolled zero-copy scanner

- Pro: no allocation, no exceptions, views over the caller buffer.
- Pro: no engine-template dependency; emits `command` directly.
- Pro: explicit framing and typed envelope errors.
- Con: only the implemented MsgTypes are understood.

### Vendored FIX engine

- Pro: complete protocol coverage including the session layer.
- Pro: maintained dictionaries and validation.
- Con: heavyweight dependency; allocation and exception behaviour outside our
  control on the hot path.
- Con: far more surface area than the order-entry subset needs.

### Field map then build

- Pro: simplest to write.
- Con: allocates a map per message on the ingress thread.
- Con: copies field values out of the buffer, defeating the zero-copy goal.

## More Information

- Implementation: `include/lob/fix_parser.hpp`.
- Tests: `tests/test_fix_parser.cpp`.
- Related: [ADR-0008](0008-single-thread-engine-spsc-boundary.md) (the command
  ring the gateway adapter will feed), the `command` tagged union in
  `include/lob/messages.hpp`.
- Reference: FIX 4.4 specification, message types D, F, G and the standard
  header / trailer (tags 8, 9, 10).
- Future work: gateway adapter into the command ring; libFuzzer harness for
  malformed inputs; FIX session layer.
