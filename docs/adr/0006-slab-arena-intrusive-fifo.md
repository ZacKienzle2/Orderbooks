---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0006. Slab arena with intrusive FIFOs for orders

## Context and Problem Statement

Each order is a small POD that must live in cache-friendly storage,
must support O(1) link / unlink into a per-level FIFO, must avoid
`new` / `delete` calls in the hot path, and must compose with optional
huge-page backing for production deployments.

## Decision Drivers

- Zero runtime heap allocation in the steady state.
- O(1) allocate and deallocate.
- Cache-line alignment on every order to avoid false sharing.
- Linkage into per-level FIFOs without per-order allocation.
- Huge-page backing on Linux for L1 / L2 cache warm-up under bursts.

## Considered Options

- Preallocated slab arena with intrusive freelist + Boost.Intrusive
  `list_member_hook` for the per-level FIFO.
- `std::pmr::polymorphic_allocator` over a monotonic_buffer_resource.
- Pool allocator from a third-party library (e.g. mimalloc, tbb).
- Plain `new`-allocated orders with `std::list` per level.

## Decision Outcome

Chosen option: **Slab arena over preallocated, cache-aligned storage**
with an intrusive `boost::intrusive::slist_member_hook` freelist and a
separate `boost::intrusive::list_member_hook` for the per-level FIFO.
Storage is `MaxOrders` cache-line-aligned slots allocated once at
construction. Huge-page backing on Linux via `MAP_HUGETLB | MAP_HUGE_2MB`,
fall back to `aligned_alloc(64, ...)` on macOS.

### Consequences

- Positive: Allocate / deallocate are single pointer-relinks; no
  syscalls, no contention, no fragmentation.
- Positive: `order` is exactly 64 B; one order per cache line; no false
  sharing under intra-engine churn.
- Positive: Huge pages cut dTLB pressure on hosts where they are
  available; bench code checks for the backing and labels results.
- Positive: ScopeGuard handles the mmap unwind; basic exception
  guarantee on construction failures.
- Negative: Upfront memory commitment (~256 MiB at `MaxOrders = 1<<22`).
- Negative: Capacity is fixed at construction; exceeding it is a hard
  failure rather than a degradation.
- Risk: Huge-page availability varies; the engine must work without and
  log a warning on the fallback path.

## Pros and Cons of the Options

### Slab arena + intrusive hooks

- Pro: Constant-time alloc / dealloc with no allocator metadata.
- Pro: Per-order alignment guarantees rule out false sharing.
- Pro: Intrusive list hook saves one allocation per FIFO insert
  compared to `std::list`.
- Con: Fixed capacity; misuse is a panic, not a slowdown.

### `std::pmr` + monotonic_buffer

- Pro: Standard, familiar to reviewers.
- Con: monotonic_buffer does not free individual objects; cancel paths
  would leak slots until the resource is reset.
- Con: Less control over alignment per-object.

### Third-party pool (mimalloc, tbb)

- Pro: Mature, well-tuned for general workloads.
- Con: Generality cost: extra metadata per object, branch on size class.
- Con: Adds a runtime dependency.

### `new` + `std::list`

- Pro: Trivial code.
- Con: `new` / `delete` are syscall-prone, contention-prone, non-deterministic.
- Con: `std::list` allocates per node, defeating the whole point.

## More Information

- Related: [ADR-0004](0004-dense-tick-ladder-book.md) (level structure
  containing the FIFO).
- Related: [ADR-0003](0003-linux-x86-64-primary-macos-dev.md) (huge
  pages are Linux-only).
- Reference: Boost.Intrusive documentation.
