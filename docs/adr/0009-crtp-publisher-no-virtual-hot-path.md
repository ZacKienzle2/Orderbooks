---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0009. CRTP and concepts in place of virtual functions on the hot path

## Context and Problem Statement

The engine must publish fills and top-of-book deltas through a
configurable sink (SPSC ring in production, null sink in tests,
in-memory recorder in benchmarks). The customization point must not
cost a virtual call per publication.

## Decision Drivers

- Zero indirect-call overhead in the engine hot path.
- Multiple publication sinks across test, bench, and production.
- Reviewer familiarity: idiom should be recognisable, not exotic.
- Strong compile-time contract on what a Publisher provides.

## Considered Options

- C++20 concepts + CRTP-like static polymorphism through templates.
- Virtual `IPublisher` interface, with `final` on production
  implementations.
- `std::function<void(event)>`.
- Type erasure via `std::any` or a hand-rolled vtable.

## Decision Outcome

Chosen option: **C++20 concepts + template parameters** on the engine
and the match kernel. Publishers are constrained by the `Publisher`
concept (defined in `include/lob/types.hpp`) and the compiler instantiates
the engine against a concrete publisher type. Boundary adapters convert
concrete sinks into types that satisfy the concept.

```cpp
template <class P>
concept Publisher = requires(P p, fill_msg const& f, top_msg const& t) {
  { p.publish(f) } noexcept -> std::same_as<void>;
  { p.publish(t) } noexcept -> std::same_as<void>;
};

template <Publisher P, std::size_t Ticks, std::size_t MaxOrders>
class engine { ... };
```

### Consequences

- Positive: Zero indirect-call cost: the compiler inlines `publish`
  into the matching loop.
- Positive: Concepts give the reviewer a one-glance contract.
- Positive: Substitution failures from concepts produce readable
  compiler errors.
- Negative: Engine code lives in a header (template instantiation
  cost); explicit instantiation for the production publisher in one
  TU keeps build times reasonable.
- Negative: Cannot swap publishers at runtime within a single engine
  instance; need a separate engine instance per sink. Acceptable in
  practice.

## Pros and Cons of the Options

### Concepts + template parameter

- Pro: Inlining means zero overhead.
- Pro: Compile-time contract surface is strict.
- Pro: Idiomatic modern C++; project conventions prefers concepts over SFINAE.
- Con: Templates in headers.

### Virtual interface

- Pro: Single engine type, swap sinks at runtime.
- Con: Vtable lookup on every publication.
- Con: `final` helps the inliner sometimes, never reliably.
- Con: Cannot be made noexcept-clean as cleanly.

### std::function

- Pro: Easy to wire from C++ callers.
- Con: Type erasure overhead per call: indirect call + sometimes
  allocation.
- Con: Cannot enforce noexcept.

### Type erasure / hand-rolled vtable

- Pro: Single engine type.
- Con: Reinvents virtual functions, with the same indirect-call cost.

## More Information

- Related: [ADR-0001](0001-cpp20-baseline.md) (concepts require C++20).
- Reference: Vandevoorde, Josuttis, Gregor. *C++ Templates: The
  Complete Guide*, 2nd ed.
- Reference: Williams, J. (2024). *Beautiful C++*, Item on CRTP vs
  virtual.
