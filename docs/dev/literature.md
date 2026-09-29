# Literature for the open optimisations

Each open item against the paper that settles or informs it, what the paper
actually says, and what it would change here. A paper is listed only after its
abstract was read, because a title that matches is not a paper that applies.

Of the nine papers read below, two do not apply and eight roadmap items have no
literature at all. Both are recorded, because that is the point of reading an
abstract rather than a title, and of writing down a search that found nothing.

## Search tools

The `bibliography` command line queries Crossref or OpenAlex and writes one JSON
object a line, which DuckDB's `read_json` reads directly:

```bash
bibliography search --source openalex "thread per core tail latency" > hits.jsonl
bibliography abstracts 10.1109/ancs.2019.8901874
```

`bibliography --help` lists its commands, among them `works_by_doi`, `citing`
and `referenced`. USENIX, NSDI and OSDI papers often have no DOI, and
`pyzotero s2search` finds them through Semantic Scholar.

Crossref ranks systems vocabulary poorly. A result at rank one is compared
against the title asked for before it is used.

## Applies

| Open item                                  | Paper                                                                                 | What it says                                                                                                                                                                                                                                   | What it would change here                                                                                                                                                                                                 |
| ------------------------------------------ | ------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Shard-owned gateway, thread per core       | Enberg, Rao and Tarkoma, ANCS 2019, doi:10.1109/ancs.2019.8901874                     | A key-value store built on application-level partitioning and inter-thread messaging cuts tail latency by up to 71 per cent against Memcached on the same hardware and Linux. Request steering and the operating system interfaces limited it. | This is the shard-owned gateway, measured. It also identifies the part to design first, the routing of a client's connection to the shard that matches its symbol.                                                        |
| MPSC ingress for multi-gateway deployments | Feldman and Dechev, SIGAPP Applied Computing Review 2015, doi:10.1145/2835260.2835264 | An array-based FIFO ring with wait-freedom, which the authors say is the only one, and explicit methods against thread starvation and livelock. The paper names trading systems as the motivating case.                                        | The roadmap's MPSC ingress option currently has no design. This is the structure to measure a hand-written one against, on the same terms ADR-0044 used for the SPSC ring.                                                |
| Replacing TCP order entry further          | Barbette, Soldani and Mathy, ANCS 2015, doi:10.1109/ancs.2015.7110116                 | Netmap and DPDK against kernel forwarding on commodity multi-queue, multi-core, NUMA hardware, with a 2.3x speed-up as an IP router and general design principles for software packet processors.                                              | The shared-memory channel already removes the network stack for a local client, at 376 cycles against 475k. This is the reference for a remote client, and the number to beat before any kernel-bypass work is justified. |
| Arena placement and NUMA first touch       | Majo and Gross, SYSTOR 2011, doi:10.1145/1987816.1987832                              | The memory controller shares local and remote bandwidth unevenly. The authors conclude that maximising data locality does not always minimise execution time, since allocating on a remote processor can be faster.                            | This explains the allocation-affinity experiment that measured worse and was rejected. The negative result was correct and now has a citation.                                                                            |

## Applies, from the second pass over Next and Later

| Open item                                         | Paper                                                                 | What it says                                                                                                                                                                                                                                                                       | What it would change here                                                                                                                                                                                                    |
| ------------------------------------------------- | --------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Differential property tests over the shard router | Zhang, Chattopadhyay and Wang, ASE 2013, doi:10.1109/ase.2013.6693061 | Automated runtime checking of quasi-linearizability in unmodified C and C++, built on LLVM and the Inspect concurrency testing tool, and it doesn't report false violations. Quasi-linearizability is the relaxed condition that admits deliberate nondeterminism for performance. | The router and the rings are concurrent structures whose correctness is an ordering property rather than a value. This is the condition to state the property against, and the tool to measure a hand-written check against. |

## Read, and does not apply

| Paper                                                           | Why not                                                                                                                                                                                                                                                                         |
| --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Patel and Biswas, MICRO 2024, doi:10.1109/micro61859.2024.00066 | FSDetect and FSLite are extensions to the MESI protocol, evaluated in simulation. They prescribe nothing a user-space program can do, and this engine already pads independent atomics to a cache line. The paper supports that padding, and the engine doesn't need to change. |
| Nikolaev and Ravindran, PPoPP 2020, doi:10.1145/3332466.3374540 | Wait-Free Eras reclaims deleted blocks in wait-free data structures. Every ring here is a bounded array over preallocated storage, and the engine reclaims nothing from it.                                                                                                     |

## Already drawn on

These are cited where the decision they informed is recorded, and are listed
here so a search does not find them twice.

| Paper                                                                                     | Where                                                |
| ----------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| Bershad et al., Lightweight Remote Procedure Call, doi:10.1145/77648.77650                | ADR-0048, the shared-memory channel                  |
| Bershad et al., User-level interprocess communication, doi:10.1145/103720.114701          | ADR-0048, the same                                   |
| Claessen and Hughes, QuickCheck, doi:10.1145/351240.351266                                | ADR-0049, the generated property tests               |
| Langdale and Lemire, Parsing gigabytes of JSON per second, doi:10.1007/s00778-019-00578-5 | ADR-0047, the structural index measured and rejected |

## Searched, and the literature prescribes nothing

Queries over the roadmap's Next and Later sections, ten in all, returned nothing
on point for these. Recorded with what the searches actually surfaced, so the
next session does not run them again.

The pattern is that each of these is a specification or a product decision
rather than a research question. An exchange publishes the protocol, a regulator
publishes the control, and there is no structure for a paper to prescribe.

| Item                                   | What the searches returned instead                                                                                                                                                                                         |
| -------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ITCH 5.0 feed handler                  | High-throughput sequencing protocols from Protocol Exchange. Crossref reads the words without their systems meaning, and ITCH is a published specification to follow rather than a design to derive.                       |
| L3 egress with delta compression       | Generic delta encoding from the Data Compression Conference, on LZSS files and virtual-machine memory. The results ignore a book's update structure, which is where the compression would come from.                       |
| Risk gateway, pre-trade limits         | Market microstructure economics, including Hasbrouck and Saar's "Low-Latency Trading" at 72 citations. These study what latency does to a market, not how to build a gateway that checks a limit.                          |
| Fat-finger guards                      | Price-limit studies from the Istanbul and Korean exchanges. Like the risk gateway results, they study effects on volatility and not how to build the guard.                                                                |
| Order-routing simulator, venue latency | Execution-quality and routing-cost economics. The one systems result was an unpublished 2026 SSRN preprint with no citations.                                                                                              |
| Post-only and pegged time in force     | Hidden-liquidity estimation and liquidity-imbalance studies. Order-type semantics come from the venue's rulebook.                                                                                                          |
| Python bindings for backtests          | pybind11 used to embed an interpreter in OpenFOAM, and HPC optimisation solvers. The results skip binding overhead on a hot path, the one question that matters here.                                                      |
| Deterministic replay                   | BugNet and QEMU-based record and replay, which reconstruct an execution that was not designed to be reproducible. This engine already replays from any prefix by sequence number, so the problem is solved upstream of it. |

## What has no paper yet

Recorded so the next search starts here rather than from nothing.

- Adopting hffix, which is a licence and error-model question rather than a
  research one.
- Boost.Interprocess for `shm_region`, the same.
- The FIX parser's remaining headroom at roughly 1,850 instructions per parse
  and near-peak instructions per cycle. Searches for vectorised parsing of
  delimited financial protocols returned nothing on point, and the JSON work
  above was already measured and rejected at order-entry message sizes.
