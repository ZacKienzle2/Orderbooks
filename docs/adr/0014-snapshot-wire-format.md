---
status: "Accepted"
date: "2026-05-20"
deciders: ["Zac Kienzle"]
---

# 0014. Snapshot wire format for warm-start

## Context and Problem Statement

A matching engine that loses state on every restart is unusable in
production. Operators need to roll the engine, deploy new binaries, or
recover from a crash without losing the resting book or the sequence
counter. The serialisation format is a long-lived ABI commitment;
choosing it carelessly creates a forward-compatibility burden that
every future code change has to honour.

## Decision Drivers

- Warm-start must reproduce the live book bit-exactly: same FIFO
  ordering at every level, same sequence counter, same top-of-book
  baseline so the next emitted top_msg fires only when state actually
  changes.
- The format must be portable across host instances of the engine but
  not necessarily across architectures (single-host warm-start is the
  primary use case; cross-host snapshot transfer is out of scope).
- The encode and decode paths must be allocation-free on the hot path
  and free of dynamic dispatch.
- The header must be self-describing so a mismatched restore fails
  fast and visibly rather than silently corrupting state.
- Future schema evolution should not require a rewrite.

## Considered Options

- Fixed-layout POD header plus packed-record body, little-endian native.
- Protobuf / Cap'n Proto for the snapshot blob.
- JSON Lines (one record per line, same shape as the event recorder).
- Direct memcpy of the engine's internal representation (book + arena
  + bitmap + id_index) as opaque bytes.

## Decision Outcome

Chosen option: **Fixed-layout POD header plus packed `snapshot_order_record`
body, native little-endian** (the only target architectures - x86_64
and arm64 macOS - are both little-endian).

```text
[snapshot_header 80 B] [snapshot_order_record 32 B] x num_orders
```

The header carries a four-byte magic "LOBS", a wire version, the
engine's template parameters (Ticks, MaxOrders), the engine_config
flags (self_cross policy, top_throttle), the seq counter, the last
published top-of-book snapshot, and the number of records that follow.
Each record carries id, remaining, px, side, tif, account_id; bids
are emitted first in (price ascending, FIFO front-to-back) order,
followed by asks in the same order.

restore() validates magic and version, rejects shape mismatches against
the engine instance's template parameters, clears any existing state,
and replays the records in arrival order so FIFO time priority at each
level is preserved.

### Consequences

- Positive: encode and decode are noexcept and one memcpy per field;
  no allocation, no parsing.
- Positive: the header is fixed at 80 bytes; restore validates
  parameters before touching the body, so a mismatch never corrupts
  the engine.
- Positive: warm-start equivalence (drive an identical post-restore
  stream through original + restored engines) is a single property
  test that asserts byte-identical fills and book state.
- Positive: schema evolution path is open: bump wire_version, add
  fields to the header and / or record, gate decode on the version.
- Negative: the format is not architecture-portable. Cross-host
  transfer needs an endianness conversion layer (not in scope).
- Negative: changes to the record schema must bump wire_version and
  ship a migrator. Disciplined but acceptable.

## Pros and Cons of the Options

### Fixed POD layout

- Pro: deterministic encode and decode cost; no parser surface.
- Pro: header is self-describing; rejection is fast and visible.
- Pro: easy to mmap and zero-copy read once we add a file sink.
- Con: schema evolution requires explicit version bumps.

### Protobuf / Cap'n Proto

- Pro: schema evolution is automatic.
- Pro: cross-architecture by construction.
- Con: pulls a runtime dependency and a code generator.
- Con: encode and decode allocate by default; tuning for zero-alloc
  is possible but unidiomatic.

### JSON Lines

- Pro: human-readable; same format as the event recorder.
- Pro: trivial to inspect with the Python viz harness.
- Con: parsing cost is unbounded; allocation per record.
- Con: numeric precision questions on uint64 values past 2^53.

### Memcpy of internal representation

- Pro: encode is a single memcpy.
- Con: format depends on the exact memory layout of book, arena,
  bitmap, id_index; any layout change breaks every existing snapshot.
- Con: pointers inside the structures cannot be persisted as-is.
- Con: tightly couples the snapshot to a specific binary build.

## More Information

- Implementation: `include/lob/snapshot.hpp`,
  `lob::engine::snapshot`, `lob::engine::restore`.
- Tests: `tests/test_engine_snapshot.cpp` covers round-trip, warm-start
  equivalence (byte-identical event streams after restore), incompatible
  shape rejection, and truncated blob rejection.
- Related: [ADR-0004](0004-dense-tick-ladder-book.md) (the dense ladder
  is what restore replays into) and
  [ADR-0006](0006-slab-arena-intrusive-fifo.md) (arena ownership of
  the records' backing storage).
