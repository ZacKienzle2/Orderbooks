---
status: "Accepted"
date: "2026-09-22"
deciders: ["Zac Kienzle"]
---

# 0040. Publish each ring slot through a sequence in its own line

## Context and Problem Statement

ADR-0008 put a bounded single-producer, single-consumer ring at each boundary,
and every order crosses two of them, ingress to its shard worker and egress to
the merger. The ring published new slots through the producer's head index. A
consumer that found its cached head exhausted loaded the index, a line the
producer writes on every push, and only then read the slot, a second line. With
the queue near empty, the regime an unloaded order sees, each handoff moved two
cache lines between cores one after the other, and the producer's next push had
to take the index line back.

Maffione, Lettieri and Rizzo (doi:10.1002/spe.2675) count the misses. A lazy
Lamport queue, which this was, pays about two per item on each side when the
consumer keeps up, and four per item in a ping-pong. Queues in the FastForward
family (Giacomoni, Moseley and Vachharajani, doi:10.1145/1345206.1345215) carry
the synchronisation inside the slot and pay one, and their improved variant
measured a third lower latency than a Lamport queue on the same host.

## Decision Drivers

- Unloaded end-to-end latency is the figure an order sees and the runtime
  reports.
- Throughput under load must not fall.
- The ring's interface and its single-producer, single-consumer contract stay as
  they are, so no caller changes.

## Considered Options

- Keep the head index with the consumer's cached copy.
- A per-slot sequence number next to the payload, with the tail index kept for
  overrun.
- FastForward with a null sentinel the consumer writes back.

## Decision Outcome

Chosen option: **a per-slot sequence number**, because it halves the lines a
handoff moves and measured faster on latency and on throughput.

Each slot holds the payload and a sequence. The producer writes the payload and
stores the slot's sequence, its position plus one, with release. The consumer
polls the slot it expects next and reads the payload once the sequence matches,
so it never reads the producer's cursor. The sequence grows by the capacity on
every lap, so a value left from an earlier lap never matches. This is the cell
sequence of Vyukov's bounded queue, which ADR-0008 named, reduced to one
producer and one consumer. A slot is sized to a power of two up to a cache line,
so none straddles two lines, and a command or an event, 40 and 48 bytes, takes a
64-byte slot.

The consumer's tail index still gates the producer against overrun, read through
the producer's cached copy as before, and the consumer publishes it once per
claim. A batch claim counts the published slots from the tail, and those
sequence loads are independent, so the core issues them together.

A null sentinel would need a payload value reserved as empty, which a command or
event does not have, and a write-back by the consumer to every slot, a second
miss per item that the tail index avoids.

Measurements on one host, a Zen 5 part under WSL2, both builds alternating
within one invocation:

| Measure                                               |  Before |   After |
| ----------------------------------------------------- | ------: | ------: |
| `bench_round_trip` median of 7 runs, ns               |     151 |      88 |
| `lob_loadgen --pin` throughput median of 9, Morders/s |   104.2 |   148.0 |
| `lob_loadgen` unloaded p50, reference cycles          |     940 |     775 |
| `lob_loadgen` unloaded p99, reference cycles          |  26,031 |  18,095 |
| `lob_loadgen` unloaded p99.9, reference cycles        | 162,815 | 141,055 |

The p50 was lower in all nine pairs. The streaming `bench_producer_consumer`
stayed inside its spread.

That harness started its latency phase with the egress rings still full and
counted no dropped events. Once it drained the backlog first and reported drops,
nine further runs with only this file differing gave 111.9 against 167.8
Morders/s at the median, 102,097 against 32,579 dropped events, and 914 against
707 cycles at p50 and 25,391 against 6,935 at p99. The merger sheds fewer events
at the higher rate, because it too no longer reads a line the producer rewrites
on every push.

### Consequences

- Positive: a handoff moves one line, and the round trip across two rings fell
  by two fifths.
- Positive: the worker no longer reads a line the producer writes on every push,
  which is where the throughput gain comes from.
- Negative: a slot takes a whole line for a 40 or 48 byte payload, so the rings
  of a four-shard loadgen runtime grow from about 15 to 20 MiB.
- Negative: a single thread that fills and drains a ring of 64-bit integers,
  `bench_burst_then_drain`, runs about three times slower, since its slots
  double to 16 bytes and each push stores twice. No boundary in the runtime
  carries integers.

## Pros and Cons of the Options

### Head index with a cached copy

- Pro: dense slots, several small payloads to a line.
- Con: two serial line transfers per handoff when the queue runs near empty.

### Per-slot sequence

- Pro: one line per handoff, any trivially copyable payload.
- Con: a line per slot for payloads near a line in size.

### FastForward with a null sentinel

- Pro: no tail index at all.
- Con: needs a reserved empty value and a consumer write to every slot.

## More Information

- Amends ADR-0008, whose boundary rings and contract are unchanged.
- Implementation: `include/lob/spsc_ring.hpp`.
- Tests: `tests/test_spsc.cpp`, including batch claims against a concurrent
  producer across many laps, and the threaded runtime suites under
  ThreadSanitizer.
- Measurement: `bench/bench_spsc.cpp` (`bench_round_trip`), `lob_loadgen`.
