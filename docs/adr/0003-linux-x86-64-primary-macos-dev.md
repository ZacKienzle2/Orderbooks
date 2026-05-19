---
status: "Accepted"
date: "2026-05-19"
deciders: ["Zac Kienzle"]
---

# 0003. Linux x86_64 as primary target, macOS for development

## Context and Problem Statement

Sub-microsecond latency claims are credible only on hardware where the
engineer controls kernel scheduling, hugepages, CPU pinning, and
interrupt routing. macOS does not expose those knobs. The day-to-day
development environment, however, is macOS on Apple Silicon.

## Decision Drivers

- Latency benchmarks must run on a configuration that resembles a real
  HFT host.
- The author iterates on macOS for ergonomic reasons.
- CI must cover both platforms to catch portability regressions.
- Some Linux-only syscalls (`sched_setaffinity`, `MAP_HUGETLB`,
  `perf_event_open`) are load-bearing for production numbers.

## Considered Options

- Linux x86_64 primary, macOS arm64 development, portable everywhere
  else.
- Linux only.
- Fully portable (Linux + macOS + Windows).

## Decision Outcome

Chosen option: **Linux x86_64 primary, macOS arm64 dev**, with no
Windows support. Linux-only features (hugepages, CPU pinning, perf
counters) live behind `#ifdef __linux__` and degrade gracefully on
macOS (warn-and-continue, fall back to portable equivalents).

### Consequences

- Positive: Latency claims have credible production-shaped hardware
  numbers behind them.
- Positive: macOS dev loop stays fast for the author.
- Positive: No Windows surface area to maintain.
- Negative: Two CI matrix axes (OS + compiler), longer pipelines.
- Negative: macOS bench numbers are reference-only, not headline numbers.
- Risk: Apple Silicon and x86_64 have different memory ordering, false-
  sharing thresholds, and SIMD widths; tests must run on both.

## Pros and Cons of the Options

### Linux primary, macOS dev

- Pro: Honest about where the engine is fast vs where it just builds.
- Pro: macOS keeps the dev loop ergonomic.
- Pro: CI catches ifdef regressions before they reach production.
- Con: Two code paths in a few infrastructure spots.

### Linux only

- Pro: One code path, simplest mental model.
- Con: No local dev on this user's primary machine without a VM or
  remote dev container, slowing iteration.
- Con: Loses portability lint that catches subtle bugs (signedness,
  endianness, alignment assumptions).

### Fully portable

- Pro: Maximum user base.
- Con: Windows support adds io_uring vs IOCP vs epoll abstractions and
  three compiler vendors to test.
- Con: Latency claims become squishy because the engine has to abstract
  over wildly different schedulers.
- Con: Author has no Windows host to iterate on.

## More Information

- Related: [ADR-0002](0002-cmake-vcpkg-manifest-mode.md) (toolchain
  matrix).
- Related: [ADR-0006](0006-slab-arena-intrusive-fifo.md) (hugepage
  backing is Linux-only; macOS falls back to `aligned_alloc`).
