---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0013. Self-cross policy is a construction-time configuration

## Context and Problem Statement

A "self-cross" occurs when an incoming aggressor would match against a
resting order from the same account. Different venues handle this
differently; the engine must support multiple policies without
hardcoding one.

## Decision Drivers

- Policy varies by venue and by client requirement.
- Policy must apply on every match cycle, including modifies that
  cross.
- Policy choice must be cheap at construction and free at run time.
- Default must be safe (no accidental self-execution).

## Considered Options

- Construction-time enum + branch in the matching kernel.
- Construction-time policy class via CRTP; specialise the kernel per
  policy.
- Runtime policy function pointer.
- Single hardcoded policy.

## Decision Outcome

Chosen option: **Construction-time enum** read into the engine. The
matching kernel branches on the enum once per match cycle, not per
fill. Default = `cancel_newest`.

```cpp
enum class self_cross_policy : std::uint8_t {
  cancel_newest    = 0,   // drop the aggressor; default
  cancel_oldest    = 1,   // cancel the resting order, continue matching
  decrement_trade  = 2,   // match at zero economic effect, emit self-trade event
};
```

### Consequences

- Positive: Single source of policy; reviewers see the enum in
  `engine_config`.
- Positive: Branch is highly predictable (one outcome per engine
  instance).
- Positive: Switching policy is a config change, not a code change.
- Negative: A per-instance branch in the cross loop. Measured impact
  on bench is below noise; if it ever matters we promote to CRTP.
- Risk: Future per-order policy overrides would require either a per-
  order tag or a different abstraction. Not in scope.

## Pros and Cons of the Options

### Construction-time enum

- Pro: Trivial to implement and review.
- Pro: Predictable branch; effectively free at run time.
- Pro: Easy to extend with new policies.

### CRTP / template specialisation

- Pro: Zero branch; compiler dead-codes the unused paths.
- Con: Three engine instantiations instead of one (binary bloat,
  longer compile).
- Con: Switching policy at deploy time requires a different binary.

### Runtime function pointer

- Pro: Maximum flexibility.
- Con: Indirect call per cross cycle; same cost as virtual.
- Con: Hard to enforce noexcept.

### Hardcoded

- Pro: Simplest code.
- Con: Venue / client variation is real; one policy fits none.

## More Information

- Related: [ADR-0012](0012-tif-coverage-stage1-gtc-ioc-fok.md)
  (interaction with TIF semantics).
- Reference: ISO 10383 venue MIC lists for which venues enforce which
  policy.
