---
status: "Accepted"
date: "2026-09-23"
deciders: ["Zac Kienzle"]
---

# 0050. Coverage-guided fuzzing at the trust boundaries

## Context and Problem Statement

Three places in this system read bytes the process did not write: the FIX parser
reads a socket, snapshot restore reads a file or a peer, and the gateway's wire
decode reads fixed-size records off a connection. Each was covered by examples
and, after ADR-0049, by properties. Both kinds of test share a limit: the inputs
come from someone deciding what to try. A property generates from a domain a
person described, and an example is a case a person thought of. Neither explores
the space of malformed input, which is exactly what an attacker does and exactly
where a parser fails.

`LOB_BUILD_FUZZ` and a `linux-clang-fuzz` preset already existed. Neither did
anything, because there were no harnesses behind them.

## Decision Drivers

- Input selection should come from the code's own behaviour, not from a list.
- A failure should be a reproducer file, not a description.
- The oracle should be as close to "nothing bad happened" as possible, so the
  harness states little and the tools state the rest.
- It must run in CI on the code it covers, not only on demand.

## Considered Options

- Keep properties only, and add examples as gaps appear.
- libFuzzer, which ships with clang and is already implied by the preset.
- AFL++ or honggfuzz, which would each need installing and wiring.

## Decision Outcome

Chosen option: **libFuzzer harnesses under AddressSanitizer and
UndefinedBehaviorSanitizer**, one per boundary, run in CI and on a schedule.

Four harnesses live in `fuzz/`:

- `fix_raw` feeds arbitrary bytes to the parser and explores the framing: short
  buffers, bad BeginString, malformed BodyLength, truncated fields.
- `fix_framed` treats the input as a message body and computes a correct
  BodyLength and CheckSum around it. Without this the fuzzer almost never
  produces a frame that validates, so it never reaches a field; with it, the
  budget goes on the grammar rather than on rediscovering arithmetic.
- `snapshot_restore` feeds arbitrary bytes to restore, and asserts that a
  restore which succeeds leaves a book whose levels agree with their FIFOs and
  which can be written and read back again, while one that fails leaves nothing
  behind.
- `gateway_wire` chunks the input into wire records, applies each through the
  gateway's validate-and-dispatch step, and asserts the book is still a book.

The oracle is mostly the sanitizers: an out-of-bounds read, an uninitialised
value, undefined behaviour or a hang fails the run without the harness saying
anything. The harnesses add only what a sanitizer cannot see, which is that a
successful parse consumes a frame inside its buffer, that ids never take the
values the index reserves, and that the book's own structural invariants hold.

The first run paid for the work. The gateway's validator switched on the `op`
byte with a `default` arm, and so did the dispatch, which meant any value above
1 was silently treated as a modify. A client's typo, or a hostile record, would
have repriced one of its own resting orders. The op byte now has a `wire_op`
enumeration, values outside it are refused before dispatch, and the dispatch
switch has no default arm, so a future op added to the enumeration fails to
compile until it is handled.

Measured on the development host, 30 seconds per harness: 21.5 million
executions of `fix_raw`, 581 thousand of `fix_framed`, 541 thousand of
`snapshot_restore` and 274 thousand of `gateway_wire`, with no crash, no
sanitizer report and no timeout.

How much that is worth is measurable, because a corpus can be replayed under
coverage instrumentation. Replaying the two parser corpora and reporting with
llvm-cov:

| Corpus age             | Parser lines | Parser branches |
| ---------------------- | -----------: | --------------: |
| 30 seconds per harness |        50.6% |           44.9% |
| a few minutes          |        95.3% |           93.4% |

The unit suite reaches 78.1% of the parser's lines and 72.1% of its branches. A
corpus grown for a few minutes therefore covers the parser further than every
hand-written parser test put together, which is the argument for keeping the
corpora as CI artifacts rather than starting cold: the coverage is in the
corpus, and a run that starts from it begins where the last one stopped.

### Consequences

- Positive: the parser and the restore path are now tested by something that
  does not need to be told what to try.
- Positive: a crash arrives as a file that replays under the same binary.
- Negative: the preset builds without tests or benchmarks, because libFuzzer
  supplies `main` and every other executable has its own; the fuzzer flag is
  therefore scoped to the harness targets rather than set project-wide.
- Negative: a scheduled long run costs CI minutes, and a corpus that grows will
  eventually want pruning with `-merge=1`.
- Fuzzing covers the paths that parse. It says nothing about the engine's
  matching semantics, which is what the model-based properties are for.

## More Information

- Related: ADR-0018 (the parser), ADR-0031 (the gateway), ADR-0014 (the snapshot
  format), ADR-0049 (the properties this complements).
- Run one: `just fuzz fix_framed 60`, or every harness with `just fuzz-all`.

- Serebryany, K. (2016). Continuous fuzzing with libFuzzer and AddressSanitizer.
  _IEEE Cybersecurity Development_. <https://doi.org/10.1109/SecDev.2016.043>
- Serebryany, K., Bruening, D., Potapenko, A., & Vyukov, D. (2012).
  AddressSanitizer: a fast address sanity checker. _USENIX ATC_.
  <https://www.usenix.org/conference/atc12/technical-sessions/presentation/serebryany>
- Bohme, M., Pham, V.-T., & Roychoudhury, A. (2016). Coverage-based greybox
  fuzzing as Markov chain. _CCS_. <https://doi.org/10.1145/2976749.2978428>

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
