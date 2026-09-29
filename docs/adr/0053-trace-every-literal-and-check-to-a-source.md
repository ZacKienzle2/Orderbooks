---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0053. Trace every literal and every disabled check to a source

## Context and Problem Statement

Two clang-tidy checks exist to find unexplained numbers,
`readability-magic-numbers` and `cppcoreguidelines-avoid-magic-numbers`, and
`.clang-tidy` turns both off. It turns off 36 more checks as well, under one
comment for the whole block. Both magic-number checks accept any value that
initialises a named constant. A name alone satisfies them, whatever the value's
source.

This record takes the inventory with the tools, proposes a state for each group,
and leaves the check policy to its owner.

## Method

The C++ figures come from clang-tidy 18, the version the CI job runs through
cpp-linter, over the `linux-clang-rel` compilation database configured with
`LOB_BUILD_FUZZ=ON` and module scanning off, as that job configures it.
`run-clang-tidy-18` ran once with the two magic-number checks and once with
every disabled check, and findings were counted once per location. A check and
its alias report the same location, and an alias pair counts once.

Named constants were found with an ast-grep rule matching a `const`, `constexpr`
or `constinit` declaration, or a `#define`, whose initialiser is a numeric
literal expression.

Python figures come from ruff 0.16 with `PLR2004` and `PLR09` selected and the
ignore list cleared on the command line.

## The inventory

### Numbers the magic-number checks report

| Directory     | Findings | What they are                                                                                                               |
| ------------- | -------: | --------------------------------------------------------------------------------------------------------------------------- |
| `include/lob` |       33 | `64` at 18 sites, SplitMix64's shifts and multipliers, the decimal radix, the checksum mask, snapshot layout, two name caps |
| `apps`        |       45 | Percentile ranks, unit conversions, the port range, argv radix, the profiler's workload shape                               |
| `bench`       |       60 | RNG seeds, two copies of the SplitMix64 step, operation counts, book depths, percentile ranks                               |
| `fuzz`        |        2 | The checksum mask and a buffer length                                                                                       |
| `tests`       |      403 | Prices, quantities and ids each test states as its expected values                                                          |
| Total         |      543 |                                                                                                                             |

The most frequent values are 100, 10, 64, 7 and 5, in that order.

### Named constants initialised from a literal

| Directory     | Count |
| ------------- | ----: |
| `include/lob` |    12 |
| `apps`        |    22 |
| `bench`       |    19 |
| `fuzz`        |     2 |
| `tests`       |    57 |
| Total         |   112 |

### The checks `.clang-tidy` turns off

With every disabled check selected, clang-tidy reports 1,859 findings. Three
disabled checks report none on this database, `readability-identifier-naming`,
`readability-redundant-access-specifiers` and
`cppcoreguidelines-prefer-member-initializer`. `readability-identifier-naming`
checks nothing until its options specify a convention, and the file sets none.

The comment says both opt-in analyzer checks report inside Catch2. That holds
for `clang-analyzer-optin.core.EnumCastOutOfRange`, whose one finding is in
Catch2's `catch_result_type.hpp`. `clang-analyzer-optin.performance.Padding`
reports 23 findings on the project's own headers, 16 in `book.hpp`, 5 in
`shard_egress_runtime.hpp` and 2 in `shard_runtime.hpp`, and none in Catch2.

| Check or alias group                                | Findings | Where                                                                       |
| --------------------------------------------------- | -------: | --------------------------------------------------------------------------- |
| `readability-identifier-length`                     |      681 | Every directory, mostly indices and short locals                            |
| `modernize-use-trailing-return-type`                |      255 | Every function with a leading return type                                   |
| Both braces-around-statements checks                |      240 | Single-statement bodies                                                     |
| `misc-include-cleaner`                              |      146 | Includes, which the include-what-you-use job in `cmake.yml` also reads      |
| `cppcoreguidelines-pro-bounds-constant-array-index` |      102 | Run-time indices, 36 in `bitmap.hpp`                                        |
| `cppcoreguidelines-pro-type-union-access`           |       84 | Reads of the event union in the engine, the publisher and tests             |
| `misc-non-private-member-variables-in-classes`      |       48 | Test fixtures and the load generator's structs                              |
| Both named-parameter checks                         |       46 | Unnamed parameters in tests, benchmarks, fuzz harnesses and the profiler    |
| `hicpp-signed-bitwise`                              |       38 | Bit operations on signed operands, mostly in benchmarks and tests           |
| `cppcoreguidelines-pro-bounds-pointer-arithmetic`   |       35 | The applications' buffers, the arena and the parser                         |
| Both vararg checks                                  |       32 | `printf` family calls, 28 in the applications                               |
| `readability-function-cognitive-complexity`         |       28 | Test bodies, and functions in `engine.hpp` and `bitmap.hpp`                 |
| `clang-analyzer-optin.performance.Padding`          |       23 | `book.hpp`, `shard_egress_runtime.hpp`, `shard_runtime.hpp`                 |
| `cert-err33-c`                                      |       16 | Unchecked C library results, 12 in the applications                         |
| `cppcoreguidelines-pro-type-reinterpret-cast`       |       14 | The arena's free list, the parser, shared memory and fuzz inputs            |
| `bugprone-easily-swappable-parameters`              |       14 | Eight in the applications, the rest in engine helpers, tests and benchmarks |
| `readability-redundant-member-init`                 |       11 | Explicit `{}` on members that default-construct                             |
| `misc-const-correctness`                            |       11 | Nine in tests, two in `bitmap.hpp`                                          |
| The three C-array checks                            |       11 | Fixed buffers, three of them in the arena                                   |
| Both array-decay checks                             |       10 | Those buffers passed to C functions                                         |
| Both member-init checks                             |        5 | Event, order and snapshot records left uninitialised                        |
| `readability-container-size-empty`                  |        3 | Tests                                                                       |
| `bugprone-sizeof-expression`                        |        2 | The arena's intrusive free list                                             |
| `bugprone-multi-level-implicit-pointer-conversion`  |        2 | The arena's intrusive free list                                             |
| `cppcoreguidelines-avoid-const-or-ref-data-members` |        1 | The stream reference in `json_recorder.hpp`                                 |
| `clang-analyzer-optin.core.EnumCastOutOfRange`      |        1 | Catch2                                                                      |

### Python

`PLR2004` reports nine comparisons with a literal, one in `bench_ci.py` and
eight in the harness tests. `PLR09` reports none. The cookie template's
`pyproject.toml` ignores both, so a change there is a template decision.

## Tracing the engine's literals

Each engine literal ends in one of four states. A documented default replaces
it, a transcribed paper or a tool's measurement derives it, a standard or
library constant replaces it, or the owner decides it.

| Literal                                                                                                                 | Proposed state     | Source                                                                                                                                                                                                                                                                                            |
| ----------------------------------------------------------------------------------------------------------------------- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `alignas(64)` at 14 sites, and the 64 in `slot_align`                                                                   | One named constant | `std::hardware_destructive_interference_size` is unusable. clang 18 with libstdc++ does not declare it, and GCC 13 warns when a header uses it and advises a constant of the project's own. clang 20 and GCC 13 both define `__GCC_DESTRUCTIVE_SIZE` as 64 for x86-64.                            |
| `W = 64` in `bitmap.hpp`, `64U` in `hash.hpp`                                                                           | Standard constant  | `std::numeric_limits<std::uint64_t>::digits`                                                                                                                                                                                                                                                      |
| SplitMix64's shifts and multipliers in `hash.hpp`                                                                       | Named and cited    | They define the published function, Steele, Lea and Flood, OOPSLA 2014, doi:10.1145/2660193.2660195, which is not yet in the literature review.                                                                                                                                                   |
| The SplitMix64 step in `bench_engine.cpp`, `bench_id_index.cpp` and `apps/profile`                                      | Defined once       | `bench_engine.cpp` and `bench_id_index.cpp` retype the whole finaliser and the profiler retypes the Weyl increment. The increment belongs beside `lob::splitmix64` and all three call it.                                                                                                         |
| `max_tag_digits = 9`                                                                                                    | Standard constant  | `std::numeric_limits<int>::digits10`, which is 9 and is the property the comment argues from                                                                                                                                                                                                      |
| `checksum_field_width = 7`, the `0xFFU` mask and the radix `10`                                                         | Derived            | The FIX specification fixes the CheckSum field as tag 10 with three digits, the checksum as the byte sum modulo 256, and integer fields as ASCII decimal.                                                                                                                                         |
| `char name[16]` in `shard_worker.hpp`                                                                                   | Documented limit   | The `pthread_setname_np(3)` manual limits a thread name to 16 bytes including the terminator.                                                                                                                                                                                                     |
| `200` in `shm_channel.hpp`                                                                                              | Owner              | `NAME_MAX` bounds a POSIX shared memory name. Moving the cap from 200 to 255 changes which names are accepted.                                                                                                                                                                                    |
| `huge_2mb = 21 << 26` fallback                                                                                          | Derived            | The `mmap(2)` manual encodes the base-2 logarithm of the page size at `MAP_HUGE_SHIFT`, so the fallback is `std::countr_zero(2 MiB) << MAP_HUGE_SHIFT`.                                                                                                                                           |
| `default_spin_budget = 1024`                                                                                            | Measurement        | Karlin et al. 1991, section 2.4, set fixed-spin's threshold to C or C/2, where C is the cost of blocking in spins. The comment measures C between 280 and 1,370 hints on the development host. 1024 sits inside that range. A measurement of C on the host that runs the engine would replace it. |
| `max_message_size = 4096`                                                                                               | Owner              | The FIX specification doesn't set a maximum. The value is a choice.                                                                                                                                                                                                                               |
| `max_order_qty = 1 << 32`                                                                                               | Owner              | A policy limit, argued from overflow.                                                                                                                                                                                                                                                             |
| `batch = 64` in `shard_worker.hpp`, `batch_max{64}` in `egress_merger.hpp`, `default_capacity_ = 256` in `id_index.hpp` | Measurement        | The comments beside them cite neither a measurement nor a source.                                                                                                                                                                                                                                 |
| `line_capacity = 384`, `max_fields = 6`                                                                                 | Derived            | The comment bounds a line by key width, twenty digits a value and punctuation. Twenty is `std::numeric_limits<std::uint64_t>::digits10 + 1`, and the rest can be computed from the keys.                                                                                                          |
| Snapshot padding and `sizeof` assertions                                                                                | Keep               | They define the snapshot's byte layout, so the format is their source.                                                                                                                                                                                                                            |

## Decision Drivers

- The rule in force keeps a check on and fixes its findings.
- A named constant that satisfies the check still needs a source.
- A change to the check policy, or to the template's `pyproject.toml`, is the
  owner's.

## Considered Options

- Turn both magic-number checks on everywhere and fix all 543 findings.
- Turn them on for `include`, `src`, `apps`, `bench` and `fuzz`, fix those 140,
  and give `tests/.clang-tidy` a documented reason to keep them off.
- Keep them off and trace the engine's literals by review alone.

## Decision Outcome

The proposal is the second option, after the three zero-finding disables are
removed. A test's literals are the expected values it states, and naming each
one would move the value away from the assertion that reads it. The engine,
application, benchmark and fuzz literals follow the table above.

The comment block gives a reason for the brace checks, run-time array indexing,
uninitialised records, the C arrays, the arena's pointer punning, the naming
convention, reference members and the two analyzer checks. The measurement
contradicts one of those reasons, since Padding reports on project headers and
not in Catch2. Each remaining disable needs its reason written against the
check's documentation, and `misc-include-cleaner` overlaps the
include-what-you-use job, so one of the two goes.

### Consequences

- Positive: every number in the engine cites the paper, measurement, standard
  constant or decision it comes from.
- Positive: three checks turn on with no findings to fix.
- Negative: four values need the owner and three need a measurement before the
  checks can pass on `include`.
- Risk: moving the cap on shared memory names changes behaviour, and changing
  the benchmarks' RNG step changes their inputs.

## More Information

- Related: ADR-0034 (cachegrind as the instrument of record), ADR-0049 (the
  property tests whose literals the tests row counts).
- Karlin, A. R., Li, K., Manasse, M. S., and Owicki, S. (1991). _Empirical
  studies of competitive spinning for a shared-memory multiprocessor_. SOSP.
  doi:10.1145/121132.286599
- Steele, G. L., Lea, D., and Flood, C. H. (2014). _Fast splittable pseudorandom
  number generators_. OOPSLA. doi:10.1145/2660193.2660195
