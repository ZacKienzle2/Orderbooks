---
status: "Accepted"
date: "2026-05-21"
deciders: ["Zac Kienzle"]
---

# 0016. NUMA-correct first-touch initialisation for the slab arena

## Context and Problem Statement

The slab arena pre-allocates `Capacity` slots of `T` on construction
and immediately writes the intrusive freelist link into every slot.
That eager initialisation touches every page of the backing storage
on the thread that constructed the arena.

Under the shard-router design, the router thread constructs each
engine (and therefore each arena) before handing it off to a per-
shard consumer thread pinned to its own CPU core. Linux's default
NUMA policy is first-touch: the kernel assigns each newly allocated
page to the NUMA node of the thread that first writes to it. The
eager freelist initialisation therefore binds every arena's pages
to the router's NUMA node, leaving the consumer thread to access
them remotely on every allocate / deallocate for the lifetime of
the engine. On a two-socket box that is a 30-50% latency penalty
per arena operation.

## Decision Drivers

- The hot path (allocate, deallocate, dereference into slot) must
  read and write storage on the local NUMA node.
- The arena's external API (constructor, allocate, deallocate,
  in_use, empty, full, capacity, owns) must not change. Existing
  callers and tests treat construction as side-effect free with
  respect to slot contents.
- Tests assert `in_use() == 0` and `empty() == true` immediately
  after construction; that invariant must hold.
- The fix must not depend on a libnuma-like external library; the
  project is single-binary by design and pulls only header / build
  dependencies.
- Implementation must be zero-cost on the steady-state allocate
  path. A first-time-init branch is acceptable if the predictor
  resolves it after one call.

## Considered Options

- **Lazy freelist initialisation in allocate**: defer the freelist
  build until the first `allocate()` call, marked unlikely so the
  branch predictor erases it from the steady-state cost.
- **Explicit `warmup_on_thread()` method**: require shard_router or
  the caller to invoke it from the consumer thread after pin.
- **Mmap with `MAP_HUGETLB | MAP_POPULATE` and explicit
  `move_pages()` calls** through libnuma to bind pages to the
  consumer's node.
- **No change**: accept the remote-NUMA penalty and rely on
  hardware prefetchers and L3 to hide it.

## Decision Outcome

Chosen option: **lazy freelist initialisation in `allocate()`**.

The constructor now only allocates the slab storage and leaves the
freelist uninitialised. The first call to `allocate()` lazily builds
the freelist; subsequent calls take a single `[[unlikely]]` branch
that the predictor resolves after the first iteration. The
consuming thread therefore first-touches every page that holds the
freelist link, and Linux's first-touch policy binds those pages to
the consumer's NUMA node automatically. No platform-specific code,
no extra dependency, no API change.

`empty()`, `full()`, `in_use()`, `capacity()` continue to read the
explicit `in_use_` counter and the static `Capacity`, so the
"in_use == 0, empty after construction" invariant is preserved.

`owns()` previously read from `storage_`'s slot array; it still
does, but the read is of the array's base address and span, not of
any slot content, so it remains valid before the freelist exists.

### Consequences

- Positive: arena slot pages are now first-touched on the consumer
  thread, putting them on the consumer's NUMA node without any
  per-allocation indirection or libnuma dependency.
- Positive: zero-cost on steady-state allocate / deallocate. The
  first-time-init branch is `[[unlikely]]` and resolves to a no-op
  after the first call.
- Positive: external API is byte-identical; no caller changes.
- Negative: the very first `allocate()` pays the cost of the full
  freelist build (linear in `Capacity`). For typical engines that
  is one-time and amortises to zero immediately.
- Negative: a process that constructs an arena and never allocates
  from it leaves slot pages cold rather than pre-populated. No
  current consumer behaves this way.
- Risk: a future caller that pre-warms the cache by reading slots
  (e.g. via `owns()` on every slot) before allocating would
  re-introduce first-touch on the wrong thread. Mitigated by
  comment in the header making the design intent explicit.

## Pros and Cons of the Options

### Lazy freelist initialisation

- Pro: minimal blast radius - one branch added to allocate.
- Pro: no platform conditionals, no external dependency.
- Pro: works under any NUMA policy the kernel ships with.
- Con: the first allocate call is slow (one-time, amortises).

### Explicit `warmup_on_thread()`

- Pro: leaves the steady-state hot path untouched.
- Pro: caller controls when the cost is paid.
- Con: every caller (engine, shard_router, tests, benches) now
  needs to remember to call warmup at the right moment; missing
  calls silently regress to remote-NUMA.

### mmap + libnuma

- Pro: explicit NUMA node selection rather than implicit
  first-touch.
- Pro: hugepage hinting available in the same call.
- Con: introduces a libnuma dependency or hand-rolled syscall
  wrapper.
- Con: needs platform conditionals; no macOS equivalent.
- Con: deferred to a future commit once the engine's hugepage
  story is broken out.

### No change

- Pro: zero work.
- Con: leaves a 30-50% per-op penalty on dual-socket hardware
  permanently; embarrasses every other optimisation in the engine.

## More Information

- Implementation: `include/lob/arena.hpp::slab_arena`.
- Related: [ADR-0006](0006-slab-arena-intrusive-fifo.md) (slab
  arena and intrusive freelist), [ADR-0015](0015-multi-symbol-shard-router.md)
  (shard router that owns the arena per engine).
- Future work: a follow-up ADR will cover hugepage backing
  (MADV_HUGEPAGE or MAP_HUGETLB) and explicit NUMA node
  pinning via mmap, once a portable allocator wrapper exists.
