# Literature behind the engine and its measurement

The ledger in [literature.md](literature.md) holds papers that settle an open
roadmap item. This file is wider. It maps the decisions this repository has
already made, or will have to make, onto the literature that bears on each one,
so that a future change argues from a citation rather than from taste.

Each DOI below came back from a backend, and its returned title was checked
against the work asked for. No DOI here was typed from memory. The **work**
column describes a paper rather than quoting its title, because the DOI is the
identifier and spelling conventions differ between a title and this document.

## Status of each row

The columns make different claims, and they are not interchangeable.

The **decision** column is this repository's own reasoning about why the work
belongs on the list. It cites nothing, because it is mine.

The **read** column says whether the abstract has been read. A row marked `no`
names a paper whose title and identity are confirmed and whose contents are not.
Treat such a row as a reading instruction, never as support for a claim, because
a title that matches is still not a paper that applies.

## Resolution method

Queries ran through the `bibliography` package in the Thesis checkout. Three
findings shaped what follows.

`bibliography.search` defaults to Crossref, and Crossref relevance on systems
vocabulary is unusable. A query for latency measurement and response time
percentiles returned a 1987 paper on stimulus omission in biological psychology.
A query about spin-then-block waiting returned a metallurgy paper about spinning
molten metal onto a chilled block. `SCOPUS_API_KEY` is set in this environment
and `search_scopus` answered the same questions correctly. The default sent
every sweep to the weaker of the two.

Asking by title is not enough on its own. Scopus returned, at rank one, a 2026
paper on combustion instabilities in a hydrogen direct-injection engine for the
query "The tail at scale". Each candidate is now scored by the fraction of the
asked title's words that appear in the returned title. The best across three
backends wins, and anything below 0.62 is rejected rather than recorded. That
turned 113 lookups into 103 accepted, and each rejection was a wrong paper
rather than a missing one.

OpenAlex returns 429 after roughly two hundred queries on the polite pool.
`OPENALEX_API_KEY` and `SCOPUS_INST_TOKEN` are both unset here.

## What invalidates a measurement

This repository reports cycles per operation, instructions per parse and
percentile latencies, and a benchmark workflow tracks them across commits. Each
of these papers describes a way for such a number to be wrong while looking
fine.

| Work                                                               | DOI                       | Read | Decision it bears on                                                                                                                                                                                                                                                                                                             |
| ------------------------------------------------------------------ | ------------------------- | ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Producing wrong data without doing anything obviously wrong, 2009  | 10.1145/1508284.1508275   | yes  | The abstract states that changing an innocuous part of an experimental setup can make a systems researcher draw the wrong conclusion, and that this bias is both large and commonplace. Nothing in the bench setup controls for link order or environment size.                                                                  |
| Statistically rigorous Java performance evaluation, 2007           | 10.1145/1297027.1297033   | no   | What a benchmark has to report beyond a mean. `bench.yml` passes `--benchmark_repetitions=5` and reports what Google Benchmark aggregates.                                                                                                                                                                                       |
| Rigorous benchmarking in reasonable time, 2013                     | 10.1145/2464157.2464160   | no   | How many repetitions at which level. The nested structure here is repetitions inside a run inside a commit, and `bench.yml` sets the innermost one.                                                                                                                                                                              |
| Performance changes reported as intervals rather than points, 2020 | 10.48550/arxiv.2007.10899 | no   | Reporting a change as an interval rather than a point. The alert threshold in the benchmark workflow is a point comparison.                                                                                                                                                                                                      |
| Virtual machine warmup blows hot and cold, 2017                    | 10.1145/3133876           | yes  | The abstract reports a changepoint method applied to the assumption that a program settles into a steady state of peak performance, which measurement methodology normally takes for granted. The engine has no virtual machine, and the same assumption is made here about cache and branch predictor state across repetitions. |
| Stabilizer, statistically sound performance evaluation, 2013       | 10.1145/2451116.2451141   | no   | Randomising layout so that a measured difference is attributable to the change rather than to where the linker put things.                                                                                                                                                                                                       |
| Coz, finding code that counts with causal profiling, 2015          | 10.1145/2815400.2815409   | no   | Which line actually limits the critical path. A sampling profile attributes time, not causality, and the two differ on a pipeline.                                                                                                                                                                                               |
| Always measure one level deeper, 2018                              | 10.1145/3213770           | yes  | The abstract says performance measurements often report surface-level results that are closer to marketing than to science. This is the discipline behind the cachegrind job.                                                                                                                                                    |
| The tail at scale, 2013                                            | 10.1145/2408776.2408794   | yes  | Latency variability is something to tolerate by construction rather than to remove, according to the abstract. So p50 is the wrong headline for a matching engine.                                                                                                                                                               |
| Tales of the tail, 2014                                            | 10.1145/2670979.2670988   | no   | Which layer a tail comes from, hardware, operating system or application. The pipeline latency work attributes to hops and has not separated these.                                                                                                                                                                              |
| Treadmill, attributing the source of tail latency, 2016            | 10.1109/isca.2016.47      | no   | Load generation and statistical treatment for a tail measurement, which is the load generator's open question.                                                                                                                                                                                                                   |

## Percentiles, and the cost of computing them

Percentiles here come from a histogram in the bench harness. These describe what
a histogram costs in accuracy and what a streaming structure guarantees.

| Work                                                              | DOI                       | Read | Decision it bears on                                                             |
| ----------------------------------------------------------------- | ------------------------- | ---- | -------------------------------------------------------------------------------- |
| Space-efficient online computation of quantile summaries, 2001    | 10.1145/375663.375670     | no   | The classical bound for an epsilon-approximate quantile in sublinear space.      |
| Effective computation of biased quantiles over data streams, 2005 | 10.1109/icde.2005.55      | no   | Relative error at the tail rather than uniform error, which is what a p99 needs. |
| Optimal quantile approximation in streams, 2016                   | 10.1109/focs.2016.17      | no   | The optimal space bound, and whether the current histogram is close to it.       |
| Computing extremely accurate quantiles using t-digests, 2019      | 10.48550/arxiv.1902.04023 | no   | A mergeable structure, which matters if per-shard histograms are ever combined.  |

## Noticing a regression without a human reading a chart

| Work                                                                      | DOI                     | Read | Decision it bears on                                                                          |
| ------------------------------------------------------------------------- | ----------------------- | ---- | --------------------------------------------------------------------------------------------- |
| Change point detection to identify software performance regressions, 2020 | 10.1145/3358960.3375791 | no   | Replacing a fixed alert threshold with a method that finds the commit where a series changed. |
| Automated system performance testing at MongoDB, 2020                     | 10.1145/3395032.3395323 | no   | The surrounding infrastructure, which `bench.yml` and github-action-benchmark approximate.    |

## Reading the machine

| Work                                                                       | DOI                         | Read | Decision it bears on                                                                                                                                                                                                |
| -------------------------------------------------------------------------- | --------------------------- | ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A top-down method for performance analysis and counters architecture, 2014 | 10.1109/ispass.2014.6844459 | no   | Classifying a stall as front end, back end, bad speculation or retiring, before optimising anything. No such method appears in the profiling table.                                                                 |
| Roofline, an insightful visual performance model, 2009                     | 10.1145/1498765.1498785     | yes  | The abstract offers the model as insight into improving both software and hardware. Applied here it decides whether the drain loop is bound by bandwidth or by compute, and so whether prefetching can help at all. |
| Can hardware performance counters be trusted, 2008                         | 10.1109/iiswc.2008.4636099  | no   | How much of a counter difference is real. Relevant to every cachegrind and perf number reported here.                                                                                                               |
| Non-determinism and overcount on hardware performance counters, 2013       | 10.1109/ispass.2013.6557172 | no   | The same question with a decade more evidence, and why one run of perf stat does not measure anything.                                                                                                              |
| Reverse engineering Intel last-level cache complex addressing, 2015        | 10.1007/978-3-319-26362-5_3 | no   | Why an array bound at a power of two collides, which the microarchitecture rules assert without a citation.                                                                                                         |
| Branch prediction and the performance of interpreters, 2015                | 10.1109/cgo.2015.7054191    | no   | Whether an indirect dispatch still costs what folklore says. The engine avoids virtual dispatch on the hot path on that basis.                                                                                      |

## Memory and allocation

| Work                                         | DOI                          | Read | Decision it bears on                                                                                                                 |
| -------------------------------------------- | ---------------------------- | ---- | ------------------------------------------------------------------------------------------------------------------------------------ |
| Reconsidering custom memory allocation, 2002 | 10.1145/582419.582421        | no   | Whether the slab arena costs less than a general allocator, which this repository assumes and has not measured against a modern one. |
| Mimalloc, free list sharding in action, 2019 | 10.1007/978-3-030-34175-6_13 | no   | The modern general allocator to measure the arena against.                                                                           |
| Making huge pages actually useful, 2018      | 10.1145/3173162.3173203      | no   | Whether backing the arena with huge pages would move the translation lookaside buffer miss rate.                                     |

## Queues, ordering and progress

The engine's correctness rests on a single-producer ring, a slab arena with an
intrusive free list, and release and acquire pairs argued in comments.

| Work                                                                                   | DOI                        | Read | Decision it bears on                                                                                                                                                                                                                     |
| -------------------------------------------------------------------------------------- | -------------------------- | ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Simple, fast and practical non-blocking and blocking concurrent queue algorithms, 1996 | 10.1145/248052.248106      | no   | The baseline every later queue is measured against.                                                                                                                                                                                      |
| Fast concurrent queues for x86 processors, 2013                                        | 10.1145/2442516.2442527    | no   | The fetch-and-add design that beats compare-and-swap rings under contention.                                                                                                                                                             |
| Cache-aware design of general-purpose single-producer single-consumer queues, 2018     | 10.1002/spe.2675           | no   | The closest published analogue to the SPSC ring here, including the batching this repository added.                                                                                                                                      |
| Hazard pointers, safe memory reclamation for lock-free objects, 2004                   | 10.1109/tpds.2004.8        | no   | Recorded as not applying, because nothing here is reclaimed, and kept on the list, so a later pass does not spend time on it again.                                                                                                      |
| Performance of memory reclamation for lockless synchronization, 2007                   | 10.1016/j.jpdc.2007.04.010 | no   | The measured comparison behind that decision.                                                                                                                                                                                            |
| Mathematizing C++ concurrency, 2011                                                    | 10.1145/1925844.1926394    | yes  | The abstract states that the draft standards, despite careful deliberation, were not yet rigorous definitions and harboured substantial problems in their details. This is the model the ring's release and acquire argument appeals to. |
| Repairing sequential consistency in C/C++11, 2017                                      | 10.1145/3062341.3062352    | no   | What the standard actually guarantees, as opposed to what the hardware happens to do.                                                                                                                                                    |
| Common compiler optimisations are invalid in the C11 memory model, 2015                | 10.1145/2676726.2676995    | no   | Whether a compiler may break an argument the source makes correctly.                                                                                                                                                                     |
| Empirical studies of competitive spinning for a shared-memory multiprocessor, 1991     | 10.1145/121132.286599      | no   | The measurement behind the spin budget in `spin.hpp`, whose comment states a competitive range without a citation.                                                                                                                       |
| Competitive randomised algorithms for nonuniform problems, 1994                        | 10.1007/bf01189993         | no   | The theory behind that range.                                                                                                                                                                                                            |
| A randomized scheduler with probabilistic guarantees of finding bugs, 2010             | 10.1145/1736020.1736040    | no   | A concurrency testing method with a stated probability, for the router property tests.                                                                                                                                                   |

## The data path

Relevant to the gateway and to any decision about replacing the transport.

| Work                                                                               | DOI                     | Read | Decision it bears on                                                                                                                                                                                                       |
| ---------------------------------------------------------------------------------- | ----------------------- | ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| The eXpress Data Path, 2018                                                        | 10.1145/3281411.3281443 | no   | In-kernel programmable packet processing, the option between a socket and full bypass.                                                                                                                                     |
| Understanding modern storage APIs, libaio, SPDK and io_uring, 2022                 | 10.1145/3534056.3534945 | no   | What an io_uring submission actually costs, for the snapshot and journal path.                                                                                                                                             |
| The IX operating system, 2016                                                      | 10.1145/2997641         | yes  | The abstract sets out a dataplane operating system giving high packet rates for small messages and microsecond-scale tail latency while keeping kernel protection. The shard-owned gateway would be arranged the same way. |
| Arrakis, the operating system is the control plane, 2015                           | 10.1145/2812806         | yes  | The abstract splits the kernel's role. An application addresses a virtualised device directly, and most input and output skips the kernel. The same argument from the other direction.                                     |
| Achieving microsecond-scale tail latency with approximate optimal scheduling, 2023 | 10.1145/3600006.3613136 | no   | Current work on the scheduling side of a microsecond service.                                                                                                                                                              |

## Encoding and compression

| Work                                                                  | DOI                       | Read | Decision it bears on                                                                                                                                                                                                         |
| --------------------------------------------------------------------- | ------------------------- | ---- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Decoding billions of integers per second through vectorization, 2015  | 10.1002/spe.2203          | no   | The vectorised integer codec family, for a level-three egress feed.                                                                                                                                                          |
| Stream VByte, faster byte-oriented integer compression, 2018          | 10.1016/j.ipl.2017.09.011 | no   | The variant whose decode is one shuffle, which suits a latency-sensitive publisher.                                                                                                                                          |
| Consistently faster and smaller compressed bitmaps with Roaring, 2016 | 10.1002/spe.2402          | yes  | The abstract reports that a hybrid of uncompressed bitmaps and packed arrays in a two-level tree beats run-length encoding on unsorted data. This is the representation to measure the price-level occupancy bitmap against. |

## Compiler and binary layout

| Work                                                      | DOI                      | Read | Decision it bears on                                                                                                       |
| --------------------------------------------------------- | ------------------------ | ---- | -------------------------------------------------------------------------------------------------------------------------- |
| BOLT, a practical binary optimizer for data centers, 2019 | 10.1109/cgo.2019.8661201 | no   | Post-link layout, which is the step after the profile-guided build this repository already has.                            |
| AutoFDO, automatic feedback-directed optimisation, 2016   | 10.1145/2854038.2854044  | no   | Collecting a profile from production rather than from a training run, which is what the profiler workloads substitute for. |
| Function placement for large-scale applications, 2017     | 10.1109/cgo.2017.7863743 | no   | Instruction cache behaviour, which nothing here measures.                                                                  |
| An evaluation of vectorising compilers, 2011              | 10.1109/pact.2011.68     | no   | How much a compiler actually vectorises, which bears on the rejected handwritten SIMD work.                                |

## Testing

| Work                                                                | DOI                     | Read | Decision it bears on                                                                                      |
| ------------------------------------------------------------------- | ----------------------- | ---- | --------------------------------------------------------------------------------------------------------- |
| Evaluating fuzz testing, 2018                                       | 10.1145/3243734.3243804 | no   | How to report what the libFuzzer targets found, including the effect of the seed and of the run duration. |
| Coverage-based greybox fuzzing as Markov chain, 2016                | 10.1145/2976749.2978428 | no   | Why corpus retention between runs matters, which the fuzz recipe already does without an argument.        |
| Are mutants a valid substitute for real faults, 2014                | 10.1145/2635868.2635929 | no   | Whether the mutation score from `nox -s mutants` means anything.                                          |
| An analysis and survey of the development of mutation testing, 2011 | 10.1109/tse.2010.62     | no   | The survey behind that question.                                                                          |

## Numerics and randomness

| Work                                              | DOI                     | Read | Decision it bears on                                                                                                                                           |
| ------------------------------------------------- | ----------------------- | ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Mersenne Twister, 1998                            | 10.1145/272991.272995   | no   | The generator most simulations default to, and the baseline for any replacement.                                                                               |
| Parallel random numbers, as easy as 1, 2, 3, 2011 | 10.1145/2063384.2063405 | no   | Counter-based generation, which gives an addressable stream per path. The profiler uses a splitmix step and the property tests need reproducibility per shard. |
| Fast reproducible floating-point summation, 2013  | 10.1109/arith.2013.9    | no   | Whether a parallel reduction can be made bitwise reproducible, and what that costs.                                                                            |
| The accuracy of floating point summation, 1993    | 10.1137/0914050         | no   | The error analysis underneath that question.                                                                                                                   |

## Named, and not resolvable to a DOI here

Each of these is a real work that the three backends did not return under a
title query, mostly because USENIX, NSDI, OSDI and NeurIPS are indexed poorly in
Crossref. They are recorded so that the next pass looks for a stable URL rather
than repeating the search.

- Rizzo, netmap, USENIX ATC 2012.
- Berger et al., Hoard, ASPLOS 2000.
- Kwon et al., Ingens, OSDI 2016.
- Ousterhout et al., Shenango, NSDI 2019.
- Fried et al., Caladan, OSDI 2020.
- Kaffes et al., Shinjuku, NSDI 2019.
- Kalia et al., Datacenter RPCs can be general and fast, NSDI 2019.
- Tene on coordinated omission, which may exist only as a talk.

## What was asked for and came back wrong

Recorded because a wrong answer that looks right is the expensive kind.

| Asked for                                 | What rank one returned                                                         |
| ----------------------------------------- | ------------------------------------------------------------------------------ |
| The tail at scale                         | A 2026 paper on combustion instabilities in a hydrogen direct-injection engine |
| Deep hedging                              | A 2026 sequel rather than the 2019 paper                                       |
| Optimisation of conditional value-at-risk | A 2024 paper on portfolio optimisation under covariance uncertainty            |
| netmap                                    | A study on speeding up Mininet that cites it                                   |
| Statistical comparisons of classifiers    | An evaluation protocol for early classifiers                                   |
| Coherent measures of risk                 | Dynamic coherent risk measures, a later and different paper                    |

Each was recovered, or recorded as unresolved, by adding the authors to the
query and scoring the title. None of them was recorded as a citation.
