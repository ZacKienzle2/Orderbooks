---
status: "Proposed"
date: "2026-09-29"
deciders: ["Zac Kienzle"]
---

# 0060. Serve each shard's sessions on its own core

## Context and Problem Statement

A worker of the threaded runtime receives commands through an ingress ring that
another thread fills, and publishes events through an egress ring that another
thread drains (ADR-0015, ADR-0019, ADR-0020). Each ring is a cross-core handoff.
ADR-0052 measured one handoff at about 42 ns on the 8-core host, and the reply
path that drains the shard rings directly at a median of 626.6 reference cycles.

The gateway and the shared-memory server of ADR-0048 work differently. The
thread that writes to the engine also reads the client's socket or ring and
executes the command itself, and the shared-memory round trip measured 376
cycles. The runtime has no front end of that kind. A deployment that puts
several shards behind one gateway adds a dispatching thread, and its two
handoffs, to each order.

Open here is whether each shard worker should serve the sessions for its symbols
and run each order to completion there, and what the transcribed literature says
about the cost and the limits.

## Considered Options

- Keep a dispatching front end that feeds the shard rings.
- Each shard worker serves the sessions assigned to its symbols and runs each
  order to completion, forwarding through the existing ring only an order for a
  symbol assigned to another shard.
- Let an idle worker take queued work from a busy one.

## Sources

The cost of a handoff. David, Guerraoui and Trigonakis measured cache coherence
on a multi-socket Xeon (SOSP 2013, doi:10.1145/2517349.2522714, section 5.2,
table 2). Loading a line that another core of the same socket holds modified
cost 109 cycles, a store to it 115, and a compare-and-swap 120. Across one
socket hop the same operations cost 289 to 324 cycles. Concord counts a
dispatcher's handoff to a worker as at least two such misses, about 400 cycles
(Iyer et al., SOSP 2023, doi:10.1145/3600006.3613136, section 2.2.2). These
agree in order of magnitude with the 42 ns ADR-0052 attributes to one handoff.

Run to completion on the core that stores the data. IX runs every stage for a
packet, network stack and application, on one core before it takes the next,
with bounded batching only under congestion, and gives each core a disjoint set
of flows through receive-side scaling (Belay et al., TOCS 2016,
doi:10.1145/2997641, section 3). The common case then doesn't cause coherence
traffic between cores, which the authors call a stronger requirement than
lock-free code, whose atomic instructions still cost (section 4.4). Enberg, Rao
and Tarkoma partition sockets and data between one pinned thread per core with
`SO_REUSEPORT`, and report tail latency up to 71 per cent below Memcached once
interrupts run on cores of their own (ANCS 2019, doi:10.1109/ANCS.2019.8901874,
sections IV.A and IV.C). Seastar's shared-nothing model is the same design, with
cross-core work passed explicitly as a message (seastar.io, shared-nothing). The
Disruptor paper traces its gains to paying a queue's fixed cost once rather than
at each stage, and to one writer per resource (Thompson et al., 2011, sections
2.2 and 3.5).

The limits. A request that arrives at a core other than the one holding its data
still has to be forwarded, and software forwarding pays for waking the thread
that stores the data. Steering in the network card is limited to flows, which
exposes the partitioning to the client (Enberg et al., sections III.A and V). A
partition that receives most of the load is limited to its one core, and the
thread-per-core prototype had higher tail latency than Memcached at low
connection counts (sections III.A and IV.C). ZygOS proves that a static,
synchronisation-free partition cannot bound the tail when service times are
widely dispersed (Prekas, Kogias and Bugnion, SOSP 2017,
doi:10.1145/3132747.3132780, section 4.1). Its remedy buffers events where an
idle core can take them (section 4.2).

## Decision Outcome

Proposed: **each shard worker serves the sessions for its symbols and runs each
order to completion**, if a prototype measures lower latency than the
dispatching front end. The runtime already gives each shard's engine one writer,
as the sources describe, and the change moves the session onto that writer's
core, as the gateway and ADR-0048 already do for one engine.

The third option is rejected. Taking work from another worker means running a
match on a book that another thread writes, which gives up the one-writer
arrangement that the Disruptor paper and ADR-0019 rely on. ZygOS's condition for
it, widely dispersed service times, is to be measured before anything is built.

### Before adoption

- Measure the dispersion of engine service times from `lob_loadgen`. A mix whose
  sweeps dominate the tail is the case ZygOS warns about.
- Prototype a worker that polls its sessions' shared-memory channels and sockets
  beside its ingress ring, and compare it with the dispatching front end on
  Friday across randomised layouts (ADR-0054, ADR-0055), open and closed loop,
  reporting the ratio of medians with its interval.
- Place each session's client and shard on one socket, since a socket hop costs
  2.5 to 4 times a handoff within one (David et al., table 2).

### Consequences

- Positive: an order from a session whose symbols belong to one shard crosses
  only the handoffs of ADR-0048, without the dispatcher's two.
- Negative: the assignment of symbols to shards becomes visible to clients, as
  Enberg et al. note for steering in the network card.
- Negative: a worker that makes a system call stalls its book. The sessions have
  to use non-blocking reads, which Enberg et al. (section I) name as a
  precondition of the design.
- Risk: a symbol that draws most of the flow is limited to one core, as it is
  today.

## More Information

- Related: ADR-0015, ADR-0019, ADR-0020, ADR-0021, ADR-0040, ADR-0048, ADR-0052,
  ADR-0054, ADR-0055.
- Perséphone reserves cores for short requests in heavy-tailed workloads
  (Demoulin et al., SOSP 2021, doi:10.1145/3477132.3483571). It applies only if
  the dispersion measurement above finds such a tail.
- Demikernel and Arrakis bear on the network path of a remote client, not on the
  handoffs inside the process, and are left to a decision about the transport
  (doi:10.1145/3477132.3483569, doi:10.1145/2812806).
