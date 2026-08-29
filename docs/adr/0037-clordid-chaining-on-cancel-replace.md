---
status: "Accepted"
date: "2026-08-29"
deciders: ["Zac Kienzle"]
---

# 0037. Modify carries the next ClOrdID

## Context and Problem Statement

A FIX OrderCancelReplaceRequest names the resting order by OrigClOrdID(41) and
assigns its next identity in ClOrdID(11). The parser read tag 11 and dropped
it, so after a replace every later request naming the order by its new id
silently no-oped. The engine had no way to express the rename.

## Decision Drivers

- FIX id chaining is the interoperability baseline for order entry.
- The rename must not disturb price-time priority semantics, which the px and
  qty branches of on_modify already own.
- Existing callers that never rename must compile and behave unchanged.

## Considered Options

- Add `new_id` to `modify_msg`; zero keeps the id.
- Translate a replace into cancel plus submit at the gateway.
- Keep a ClOrdID-to-current-id alias map outside the engine.

## Decision Outcome

Chosen option: **`modify_msg::new_id`**. The engine renames the id_index entry
before dispatching the px and qty branches, so the crossing cancel plus
resubmit also runs under the new identity. The parser requires tag 11 on `G`
and carries it through. Id uniqueness stays the caller's contract, exactly as
for submit ids.

Gateway-side cancel plus submit was rejected because it forfeits the qty-only
and in-place reprice fast paths and changes priority semantics. An alias map
duplicates the id_index and adds a lookup to every subsequent command.

### Consequences

- Positive: replace-then-cancel-by-new-id works; the differential stream
  chains a quarter of its modifies to keep it honest.
- Positive: `new_id` defaults to zero, so every existing initialiser and the
  binary gateway protocol are unchanged.
- Negative: renaming onto a live id is undefined by contract, matching the
  engine's other validated-upstream inputs.
