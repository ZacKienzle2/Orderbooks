---
status: "Accepted"
date: "2026-06-19"
deciders: ["Zac Kienzle"]
---

# 0023. Huge-page-backed slab arena

## Context and Problem Statement

The slab arena holds every resting order in one contiguous block. At a
realistic capacity that block spans megabytes, which over the default 4 KiB
page size means hundreds of pages, each needing its own data-TLB entry. A
market-data burst that touches thousands of resting orders walks across more
pages than the TLB holds, so a fraction of allocate, deallocate, and slot
dereference operations stall on a page-table walk and surface in the tail
latency.

ADR-0016 deferred huge-page backing until a portable allocator wrapper
existed, choosing lazy first-touch as the NUMA fix in the meantime. That
wrapper is the missing piece. The arena should back its slab with 2 MiB huge
pages where the host allows, without losing the first-touch behaviour and
without changing the arena's API.

## Decision Drivers

- A 2 MiB page maps 512 of the 4 KiB pages with one TLB entry, so the whole
  slab should sit in a handful of TLB slots.
- Huge pages are a best-effort host resource. The arena must run unchanged on
  a host with none reserved.
- The backing must not pre-fault, so the lazy, consumer-thread first-touch of
  ADR-0016 still binds pages to the right NUMA node.
- No external dependency and no change to the arena's public interface.

## Considered Options

- A `hugepage_region` RAII wrapper with a platform fallback chain, used as the
  arena's storage.
- `madvise(MADV_HUGEPAGE)` on the existing heap allocation only.
- A libnuma or hwloc dependency for explicit huge-page and node control.

## Decision Outcome

Chosen option: **a `hugepage_region` RAII wrapper with a fallback chain**,
used as the slab arena's storage in place of the heap `unique_ptr`.

```text
Linux:  mmap(MAP_HUGETLB | MAP_HUGE_2MB)  ->  mmap + madvise(MADV_HUGEPAGE)
macOS:  mmap(VM_FLAGS_SUPERPAGE_SIZE_2MB) ->  mmap
other:  over-aligned operator new
```

The region asks for explicit 2 MiB huge pages first; if the host reserved
none the mapping fails and it drops to a plain anonymous mapping hinted for
transparent huge pages, then to an over-aligned heap allocation. `source()`
reports which backing won, so a test or startup log can confirm the intent
held on a tuned host. The region never passes `MAP_POPULATE`, so pages stay
unmapped until first write and the arena's lazy, consumer-thread freelist
build still first-touches them on the right NUMA node.

The arena's API is byte-identical. Because the storage pointer is now an
opaque runtime value rather than a heap array the optimiser can reason about,
the arena asserts the pointer non-null with `__builtin_unreachable` on the
impossible branch; this restores the optimiser's proof that `allocate()`
returns non-null on its success path, so callers keep their null-check-free
hot path and `-Wnull-dereference` stays quiet.

### Consequences

- Positive: the slab needs only a handful of TLB entries on a host with huge
  pages reserved, removing page-walk stalls from the burst path.
- Positive: graceful degradation; an untuned host falls back transparently and
  every test runs the fallback path.
- Positive: composes with the ADR-0016 first-touch NUMA binding; no
  pre-faulting.
- Positive: no external dependency, no arena API change.
- Negative: a huge mapping rounds up to a 2 MiB multiple, so a small arena on
  a huge-page host reserves a whole 2 MiB page.
- Risk: explicit huge pages depend on host configuration (`vm.nr_hugepages`);
  `source()` and `docs/dev/threading.md` document how to confirm it.

## Pros and Cons of the Options

### hugepage_region with fallback chain

- Pro: real huge pages where available, transparent fallback everywhere else.
- Pro: header-only, no dependency, arena API unchanged.
- Con: more platform-conditional code than a single allocation call.

### madvise on the heap allocation only

- Pro: smallest change.
- Con: transparent huge pages are advisory and may never promote; no explicit
  reservation, no confirmation that the slab is huge-backed.

### libnuma or hwloc

- Pro: explicit node and page control.
- Con: an external dependency the single-binary design avoids, with no macOS
  equivalent.

## More Information

- Implementation: `include/lob/hugepage.hpp` and `include/lob/arena.hpp`.
- Tests: `tests/test_hugepage.cpp` covers a multi-page region, a sub-page
  request, and move-ownership transfer; `tests/test_arena.cpp` exercises the
  arena over the new backing unchanged.
- Related: [ADR-0016](0016-numa-first-touch-arena.md) (the first-touch policy
  this composes with and the follow-up it promised) and
  [ADR-0006](0006-slab-arena-intrusive-fifo.md) (the slab arena).
