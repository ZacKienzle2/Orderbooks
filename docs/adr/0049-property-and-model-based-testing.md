---
status: "Accepted"
date: "2026-09-23"
deciders: ["Zac Kienzle"]
---

# 0049. Property and model-based testing instead of hand-rolled streams

## Context and Problem Statement

The differential test drove the engine and the reference book with a
hand-written generator: a `std::mt19937_64` seeded from a list of literal seeds,
a command mix fixed at six submits, two cancels and two modifies in ten, prices
and quantities drawn from ranges written into the file, and four thousand
commands per run. It found real defects, but it has three faults that no amount
of care removes.

A failure arrives as the whole stream. Nothing reduces four thousand commands to
the two that matter, so the work of finding the defect begins after the test has
already found it.

The lists go stale. The self-cross policies were covered by three test cases
that named them one by one, the time-in-force values by a distribution over
three integers, and the comparison helpers by five functions naming every field
of every event. A policy, a time-in-force or a field added to the library is
covered only when someone remembers to edit each of those places, and the test
goes on passing in the meantime.

The magnitudes are arbitrary. Quantities ran to 50, accounts to 4, the arena
exhaustion case used prices banded at 100 and 156, and none of those numbers
follow from anything.

## Decision Drivers

- A failing test should report the smallest input that fails.
- Coverage of an enumeration should follow from the enumeration.
- A comparison should follow from the type being compared.
- Magnitudes should come from the generator's own scaling, not from the file.

## Considered Options

- Keep the hand-written generator and add cases as gaps appear.
- Property-based testing with RapidCheck, which is QuickCheck for C++.
- A fuzzer over a serialised command stream.

## Decision Outcome

Chosen option: **RapidCheck, in its model-based form**, with the enumerations
and the comparisons taken from libraries rather than written out.

Claessen and Hughes' QuickCheck supplies both halves of what was missing: a
property says what must hold for every input, and a failing property is shrunk
to a minimal counterexample before it is reported. Hughes' later account of
applying it to stateful systems supplies the arrangement for an order book:
model the system as a state machine, generate command sequences valid for the
model, and check the implementation after each command.

`tests/test_engine_model.cpp` is that. The model holds only what a command needs
to be generated, the next id, the live ids and the accounts already trading. The
system under test is the pair of engines driven by one command stream, and the
property is that they publish the same events in the same order and end in the
same book state.

Three libraries remove the lists:

- magic_enum enumerates `self_cross_policy`, `side` and `tif`, so a property
  runs over every enumerator and an enumerator added to the library is covered
  the day it is added.
- Boost.PFR compares the events member by member, so the test names no field and
  a field added to an event is compared from the moment it exists.
- RapidCheck's size parameter sets the magnitudes, so quantities and accounts
  scale from small to large as a run progresses rather than stopping at a number
  written here.

The bounds that remain are the engine's own: the tick ladder it was instantiated
with, the arena it was given, and the batch a prefetch distance looks ahead
within. The ladder is a machine word squared, the shape at which the
hierarchical bitmap's two levels are exactly full, and the reject property uses
the smallest arena the slab permits, so a book that cannot match rejects from
its second resting order and the shortest generated sequences reach the seam.

The test file list in `tests/CMakeLists.txt` went the same way: the suite is now
every `test_*.cpp` beside it, found by a `CONFIGURE_DEPENDS` glob, because a
list of file names drifts from the directory for the same reason.

The conversion immediately paid for itself. The handle-mode property failed on
its fifth case and shrank to two commands, a submit and a modify. The cause was
that `lob::prefetch_plan{}` default-constructs to a distance of two and one
rather than to zero, so a harness that inferred "no batching" from an empty plan
took the batched path and never recorded the handles the next command needed.
The old test could not have pointed at that, both because it had no shrinking
and because it never compared the two paths. The application path is now stated
rather than inferred.

Reported coverage from a run: between 32 and 51 percent of sequences reach a
match, up to 18 percent dispatch a self cross under `decrement_trade`, and 88
percent of the capacity sequences reach the arena limit. Those figures come from
RapidCheck's classification, so a property that stops reaching its seam is
visible rather than quietly passing.

### Consequences

- Positive: failures arrive minimised, and coverage is reported rather than
  assumed.
- Positive: three classes of staleness are gone, because the enumerations, the
  field lists and the file list are all derived.
- Negative: three more test-only dependencies, `rapidcheck`, `magic-enum` and
  `boost-pfr`, all header-light and used only under the `tests` feature.
- Negative: a property runs a hundred sequences by default rather than a fixed
  stream, so the suite's run time now varies with RapidCheck's configuration.
- The same command generators are the natural basis for the remaining
  hand-rolled streams in the invariant, bitmap, arena and snapshot tests.

## More Information

- Related: ADR-0009 (the reference engine), ADR-0038 (the prefetch distances the
  batched property covers), ADR-0042 (handle-driven lookup).
- Measurement: `lob_tests "[model]"` prints the classification above.

- Claessen, K., & Hughes, J. (2000). QuickCheck: a lightweight tool for random
  testing of Haskell programs. _ICFP_. <https://doi.org/10.1145/351240.351266>
- Hughes, J. (2007). QuickCheck testing for fun and profit. _PADL_.
  <https://doi.org/10.1007/978-3-540-69611-7_1>
- Arts, T., Hughes, J., Johansson, J., & Wiger, U. (2006). Testing telecoms
  software with Quviq QuickCheck. _Erlang Workshop_.
  <https://doi.org/10.1145/1159789.1159792>

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
