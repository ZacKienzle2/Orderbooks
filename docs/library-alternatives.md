# Handwritten components and their library alternatives

This page lists each component in this repository that does what a package also
does, with the alternative, what was measured, and what was decided. A component
appears whether the library was adopted or rejected, because the question was
asked and answered with a number rather than left to taste.

The tables prefer the standard library first and the platform second. After
those come an installed dependency and then a new one. Local code is written
only when a measurement says it is better. A row with status "unmeasured" is an
open question.

## Decided, with a measurement

| Component                 | Alternative considered                                                                   | Result                                                        | Record   |
| ------------------------- | ---------------------------------------------------------------------------------------- | ------------------------------------------------------------- | -------- |
| `id_index.hpp`            | `boost::unordered_flat_map`, `ankerl::unordered_dense`                                   | Library slower on four benchmarks, none faster. Kept local.   | ADR-0046 |
| `spsc_ring.hpp`           | `rigtorp::SPSCQueue`, `moodycamel::ReaderWriterQueue`, `boost::lockfree`, `atomic_queue` | Ring faster on round trip and streaming. Kept local.          | ADR-0044 |
| `latency_histogram.hpp`   | HdrHistogram_c                                                                           | Library adopted. The from-scratch histogram was deleted.      | ADR-0045 |
| Level FIFO                | `boost::intrusive::list`                                                                 | Library adopted at the outset.                                | ADR-0006 |
| Order storage             | `std::pmr` over a monotonic buffer                                                       | Slab arena kept. pmr indirection costs on the hot path.       | ADR-0006 |
| FIX field scan            | simdjson-style structural index                                                          | Slower at order-entry message sizes. Rejected.                | ADR-0047 |
| Inlining and block layout | Profile-guided optimisation                                                              | Adopted as a build mode, 0.5 to 2.5 per cent of instructions. | ADR-0051 |
| Aliasing hints            | `__restrict` on the ladder, as the optimisation records suggested                        | No measurable difference. Reverted.                           | ADR-0051 |

## Open, with a measurement

| Component         | Alternative                | Measurement                                                                                                                                                                              | Why it is still open                                                                                                                                                                                                                                                    |
| ----------------- | -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `fix_parser.hpp`  | hffix 1.4.1 (BSD-2-Clause) | With matched validation, hffix parses a new order single in 1,463 instructions against 1,644 here, and a cancel in 1,051 against 1,270. Counted with cachegrind.                         | Adopting it trades the parser's error enumeration, which the gateway and its tests depend on, for `is_valid` and `is_complete`. The gap narrowed from 22 per cent to 11 by taking the per-digit overflow test out of the tag scan. The rest is hffix's field iteration. |
| `shm_channel.hpp` | `Boost.Interprocess`       | `shared_memory_object` plus `mapped_region` does exactly what `shm_region` does, in about fifteen lines against a hundred and twenty, and pulls 393 headers where this header pulls 351. | Boost reports failure by exception where this returns an error code into `noexcept` paths, and its names may not contain a slash where the POSIX API this wraps requires a leading one. Both are adaptable at a cost.                                                   |

## Handwritten on purpose, with the reason

| Component                               | Nearest alternative                              | Why it stays                                                                                                                                                                                                                                                                            |
| --------------------------------------- | ------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `bitmap.hpp`                            | `std::bitset`, `boost::dynamic_bitset`, CRoaring | None of them answer "the next populated price at or after this one" in constant time. The hierarchy is the structure the integer-data-structure literature prescribes for a dense, small universe, and the ladder is exactly two machine words deep.                                    |
| `json_recorder.hpp`                     | nlohmann/json, glaze, RapidJSON                  | The format has no strings from outside the process: every key is a literal and every value an integer. There is nothing to escape and no schema to keep in step, so a library would add allocation and a locale lock without saving any work.                                           |
| `snapshot.hpp`                          | FlatBuffers, Cap'n Proto, cereal                 | The format is a fixed-layout dump of trivially copyable records, read back in one pass. A schema compiler would add a build step and a generated header to express what the struct already expresses. A schema library becomes useful once the format needs versioning across releases. |
| `hash.hpp`                              | wyhash, xxhash                                   | It is SplitMix64, a published function. Ids are client-supplied, so the five-wise independence argument against multiply-shift applies.                                                                                                                                                 |
| `hugepage.hpp`                          | none                                             | A fallback chain over `mmap` flags. Each package that abstracts explicit huge pages also performs the allocation.                                                                                                                                                                       |
| `affinity.hpp`                          | hwloc                                            | A thin wrapper over `pthread_setaffinity_np`. hwloc is a topology library, and a topology-aware placement policy would justify it. This code has no such policy yet.                                                                                                                    |
| `spin.hpp`, `tsc.hpp`                   | `std::atomic::wait`, `std::chrono`               | Both wrap one intrinsic each, deliberately: the standard facilities are the thing being avoided on these paths.                                                                                                                                                                         |
| `shard_router.hpp`, `egress_merger.hpp` | none                                             | A hash and a modulo, and a linear scan over four rings. There is no library for a four-way merge that beats reading four cursors.                                                                                                                                                       |

## Adding a row

A row follows a measurement. The measurement belongs in an ADR when it changes a
decision and here when it records that one was taken. `orderbooks.bench_ci`
reports effect sizes with confidence intervals for wall-clock comparisons;
cachegrind is the instrument when the host is not quiet, because its counts are
exact.
