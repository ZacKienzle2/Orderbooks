---
status: "Accepted"
date: "2026-09-23"
deciders: ["Zac Kienzle"]
---

# 0048. Shared-memory channel for local order entry

## Context and Problem Statement

The engine answers a submit in tens of nanoseconds, and the gateway then spends
tens of microseconds returning the ack, because a local client reaches it over a
TCP socket. Loopback still runs the whole host network stack: two system calls
per round trip, a copy in each direction, the socket buffers, and a scheduler
wake-up on each side. The transport, not the matching, is the cost a local
client measures.

## Decision Drivers

- A client on the same machine should not pay for a network it does not cross.
- The ack path must stay one record per order, so the existing wire format and
  the existing correctness checks carry over unchanged.
- No new queue: the ring, its handoff discipline and its tests already exist.
- The transport must not become a second thing to reason about when a client
  dies mid-session.

## Considered Options

- Keep the TCP socket for every client.
- A Unix domain socket, which still enters the kernel per message.
- A shared-memory channel: two rings in a segment both processes map, polled
  rather than signalled.

## Decision Outcome

Chosen option: **a shared-memory channel**, because the arrangement the
literature prescribes for a same-machine call removes every cost above, and it
is built from parts the repository already has.

Bershad, Anderson, Lazowska and Levy make the argument directly. Their LRPC
measurements found that the overwhelming majority of calls never leave the
machine, that the arguments they carry are small and of fixed size, and that a
call implemented for the cross-machine case pays stub interpretation, message
buffer management, access validation, dispatch and a scheduler rendezvous that a
same-machine call does not need. LRPC removes each of those by mapping an
argument stack pairwise into both domains at bind time and copying the arguments
once. URPC then removes the kernel from the call path entirely, passing messages
through memory the two address spaces share.

`lob/shm_channel.hpp` is that arrangement for order entry:

- Bind once. The segment is created, sized and mapped when a client attaches.
  Nothing on the message path afterwards enters the kernel.
- One copy. The client writes the request into the ring slot the server reads,
  where the socket path copies into the kernel and out again.
- No dispatch layer. The server polls its own ring and runs the command.
- The server context is already resident, which is what LRPC buys by caching
  domains on idle processors; here the serving thread spins on its core, so it
  is the steady state rather than an optimisation.
- One pair of rings per client, so two clients never touch the same line.

The rings are plain `lob::spsc_ring` objects constructed inside the mapping, so
the handoff discipline, the sequence-per-slot publication and the cache
behaviour are the ones already measured and tested. The owner constructs them
and publishes a ready word with release; a peer maps the segment, waits for that
word, and reads the objects in place. The segment holds no pointers, so the two
mappings need not land at the same address.

Measured on the 8-core host, same binary, same engine, same records, 20,000
crossing pairs closed loop, every bid filled on both transports:

| Transport     | p50 round trip | p99 round trip | Closed-loop rate |
| ------------- | -------------: | -------------: | ---------------: |
| TCP loopback  |    475k cycles |   1.92M cycles |  0.008 Morders/s |
| Shared memory |     376 cycles |     611 cycles |    7.5 Morders/s |

At the host's 5.117 GHz timestamp counter that is 73.5 ns against 93 us at the
median, a factor of about 1,260, and the shared-memory figure covers the whole
path: the client's push, the server's poll, the match, the ack push and the
client's poll. Two separate processes, each pinned, measure the same 376 cycles
as the two-mapping self-test, so nothing in the number comes from sharing an
address space.

### Consequences

- Positive: a local client's round trip is dominated by two cross-core handoffs
  and the match itself, not by the network stack.
- Positive: the TCP path is untouched and stays the transport for a client that
  is genuinely remote.
- Negative: both sides spin, so a channel costs a core on each end while it is
  open. This is the same trade the shard workers already make, and it is what
  makes the wake-up disappear.
- Negative: the segment is a shared trust boundary. The owner unlinks its name
  on destruction so a dead server leaves nothing for a later client to attach
  to, and a client that attaches to an unpublished segment backs out rather than
  spinning forever, but a client that writes nonsense into its own ring reaches
  the engine's validation rather than the kernel's.
- A disconnect is now an explicit record, where the socket path read the peer's
  close from the transport itself.

## More Information

- Related: ADR-0021 (per-shard egress), ADR-0030 (the gateway and its wire
  format), ADR-0044 (why the ring is the queue).
- Measurement: `lob_gateway` with and without `--shm`, and `--shm-serve` against
  `--shm-client` for the two-process case.

- Bershad, B. N., Anderson, T. E., Lazowska, E. D., & Levy, H. M. (1990).
  Lightweight remote procedure call. _ACM Transactions on Computer Systems_,
  8(1). <https://doi.org/10.1145/77648.77650>
- Bershad, B. N., Anderson, T. E., Lazowska, E. D., & Levy, H. M. (1991).
  User-level interprocess communication for shared memory multiprocessors. _ACM
  Transactions on Computer Systems_, 9(2).
  <https://doi.org/10.1145/103720.114701>

- Nygard, M. (2011). _Documenting Architecture Decisions_.
  <https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions>
- MADR project. <https://adr.github.io/madr/>
- ThoughtWorks Tech Radar - ADRs.
  <https://www.thoughtworks.com/radar/techniques/lightweight-architecture-decision-records>
