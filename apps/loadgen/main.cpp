// lob_loadgen
// -----------
// Drive the assembled multi-shard runtime end to end under synthetic order
// flow and report throughput and end-to-end latency. Each iteration submits a
// resting ask and a crossing bid on one symbol, so every pair produces a fill
// whose taker is the bid. The producer parks an rdtsc stamp for the bid before
// submitting; the merger thread, forwarding every shard's events into a sink,
// differences the egress stamp against it to time the order's whole journey
// through the ingress ring, the shard worker, the match, the egress ring, and
// the merge. The latency is therefore measured under load, queueing included.
//
// The latency phase is closed by default, one pair in flight, which times
// service without queueing. With --load it is open: pairs arrive at
// exponential gaps whose rate is that fraction of the rate at which the
// throughput phase delivered fills, as independent senders would submit
// them. A closed generator reports lower
// response times than an open one at the same load (Schroeder, Wierman and
// Harchol-Balter, NSDI 2006, section 5.1), and understated the p99 by more
// than two times at 80 per cent load (Treadmill, ISCA 2016, section II.A).
//
// No market data is required. The flow is generated; the symbols spread across
// shards through the same SplitMix64 routing the runtime uses.

#include <lob/config.hpp>
#include <lob/egress_merger.hpp>
#include <lob/latency_histogram.hpp>
#include <lob/messages.hpp>
#include <lob/shard_egress_runtime.hpp>
#include <lob/spin.hpp>
#include <lob/tsc.hpp>
#include <lob/types.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t ticks = 1024;
constexpr std::size_t max_orders = std::size_t{1} << 16;
constexpr std::size_t num_shards = 4;
constexpr std::size_t ingress_cap = std::size_t{1} << 14;
constexpr std::size_t egress_cap = std::size_t{1} << 16;
constexpr lob::tick_t price = ticks / 2;
constexpr std::size_t num_symbols = 64;

// Power-of-two table of submit timestamps indexed by order id. Sized far above
// the in-flight depth so an id and its echo never alias a later order's stamp.
constexpr std::size_t slots = std::size_t{1} << 20;
constexpr std::size_t slot_mask = slots - 1;

using runtime_t = lob::shard_egress_runtime<ticks, max_orders, num_shards, ingress_cap, egress_cap>;

using lob::cpu_relax;
using lob::read_tsc;

// Forwards every merged event and, for a taker fill whose submit stamp is
// parked, records the end-to-end latency. The latency phase stamps only the
// order in flight and waits on samples, so samples is atomic for the producer
// to observe across the merger thread. Satisfies the merge_sink concept.
struct latency_sink {
    std::vector<std::atomic<std::uint64_t>>& send_tsc;
    lob::latency_histogram& hist;
    std::uint64_t events{0};
    std::atomic<std::uint64_t> samples{0};
    // Set once the latency phase begins. The throughput phase stamps nothing,
    // so until then a fill skips the stamp table, a random load into 8 MiB
    // that would otherwise slow the merger it is measuring.
    std::atomic<bool> stamping{false};
    // Fills that reached the sink. A full egress ring drops events, so the
    // open loop sizes its rate on these rather than on the orders submitted.
    // The consumer is the only writer, so a relaxed load and store count them
    // without a locked add.
    std::atomic<std::uint64_t> fills{0};

    void on_event(const lob::event& e, std::uint64_t /*merge_seq*/) noexcept {
        ++events;
        if (e.k != lob::event::kind::fill)
            return;
        fills.store(fills.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        if (stamping.load(std::memory_order_relaxed)) {
            const auto t0 = send_tsc[e.body.fill.taker & slot_mask].load(std::memory_order_relaxed);
            if (t0 != 0) {
                hist.record(read_tsc() - t0);
                samples.fetch_add(1, std::memory_order_release);
            }
        }
    }
};

struct args {
    std::uint64_t orders{20'000'000};
    double load{0.0};  // 0 keeps the latency phase closed
    std::uint64_t seed{0xC0FFEE};
    bool pin{false};
    bool direct{false};
};

[[noreturn]] void usage(int code) {
    std::cerr
        << "usage: lob_loadgen [options]\n"
           "  --orders N   total orders to submit (default 20000000)\n"
           "  --load F     open-loop latency phase at F of the measured throughput, 0 < F < 1\n"
           "  --seed N     PRNG seed of the open-loop arrivals (default 0xC0FFEE)\n"
           "  --pin        pin shard workers to cores\n"
           "  --direct     no merger thread: the client drains the shard rings itself\n"
           "  --help       show this help\n";
    // std::exit is flagged mt-unsafe, but arg parsing runs single-threaded
    // before any worker is spawned.
    std::exit(code);  // NOLINT(concurrency-mt-unsafe)
}

args parse_args(int argc, char** argv) {
    args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--orders" && i + 1 < argc) {
            a.orders = std::strtoull(argv[++i], nullptr, 10);
        } else if (s == "--load" && i + 1 < argc) {
            a.load = std::strtod(argv[++i], nullptr);
            // An open queue at or above its capacity grows without bound.
            if (!(a.load > 0.0 && a.load < 1.0)) {
                std::cerr << "--load needs 0 < F < 1\n";
                usage(2);
            }
        } else if (s == "--seed" && i + 1 < argc) {
            a.seed = std::strtoull(argv[++i], nullptr, 0);
        } else if (s == "--pin") {
            a.pin = true;
        } else if (s == "--direct") {
            a.direct = true;
        } else if (s == "--help") {
            usage(0);
        } else {
            std::cerr << "unknown argument: " << s << "\n";
            usage(2);
        }
    }
    return a;
}

// What the direct client claims from one shard per visit, and how often it
// visits while producing. The batch is the merger's own bound, so both paths
// claim the same way and the comparison is of the hop rather than of the claim
// size. The interval is that bound shared among the shards, so a producer
// running flat out still drains each ring before it can overflow.
constexpr unsigned poll_batch_max = lob::merger_config{}.batch_max;
constexpr std::uint64_t poll_interval = poll_batch_max / runtime_t::shard_count();

lob::submit_msg ask(lob::order_id_t id) noexcept {
    return {.id = id,
            .px = price,
            .qty = 1,
            .s = lob::side::ask,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0};
}

lob::submit_msg bid(lob::order_id_t id) noexcept {
    return {.id = id,
            .px = price,
            .qty = 1,
            .s = lob::side::bid,
            .t = lob::tif::ioc,
            ._pad = 0,
            .account_id = 0};
}

int run(const args& a) {
    std::vector<std::atomic<std::uint64_t>> send_tsc(slots);
    lob::latency_histogram hist{10'000'000, 3};

    const lob::shard_runtime_config rt_cfg{
        .pin_threads = a.pin, .first_core = 0, .core_stride = 1, .spin_budget = 1024};
    // The runtime embeds every shard's ingress and egress ring inline, tens of
    // megabytes in total, so it lives on the heap rather than the stack.
    const auto rt_ptr = std::make_unique<runtime_t>(lob::engine_config{}, rt_cfg);
    runtime_t& rt = *rt_ptr;
    latency_sink sink{send_tsc, hist};
    lob::egress_merger<runtime_t, latency_sink> merger{rt, sink,
                                                       lob::merger_config{.pin_thread = false}};

    // The merged stream is one consumer of the per-shard rings, not the only
    // one there could be (ADR-0021). With --direct the client drains them
    // itself, which takes the merger thread out of the reply path: an event
    // then crosses one core boundary, from the shard that produced it to the
    // client, where the merged path crosses two. The client remains a single
    // consumer per ring, which is the contract spsc_ring states.
    const auto poll_all = [&rt, &sink]() noexcept {
        unsigned n = 0;
        for (std::size_t s = 0; s < runtime_t::shard_count(); ++s) {
            n += rt.poll_batch(s, poll_batch_max,
                               [&sink](const lob::event& e) noexcept { sink.on_event(e, 0); });
        }
        return n;
    };

    rt.start();
    if (!a.direct)
        merger.start();

    lob::order_id_t next = 1;

    // Phase 1: throughput. Fire every order at max rate without stamping, so
    // the saturated queueing delay is never mistaken for processing latency.
    const std::uint64_t pairs = a.orders / 2;
    std::uint64_t submitted = 0;
    if (a.load > 0.0 && pairs == 0)
        throw std::invalid_argument(
            "--load scales the throughput phase, which --orders left empty");
    const auto wall0 = std::chrono::steady_clock::now();
    const std::uint64_t tsc0 = read_tsc();
    for (std::uint64_t i = 0; i < pairs; ++i) {
        const lob::symbol_id_t sym = i % num_symbols;
        while (!rt.try_submit(sym, ask(next++))) {
        }
        while (!rt.try_submit(sym, bid(next++))) {
        }
        submitted += 2;
        // Nobody else is draining in direct mode, so the producer has to, or
        // the rings fill and the publisher starts dropping events.
        if (a.direct && (i % poll_interval) == 0)
            (void)poll_all();
    }
    rt.drain();
    const auto wall1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(wall1 - wall0).count();

    // The saturated phase can leave the egress rings full behind a merger that
    // trails the workers, and a full ring drops an event rather than block. A
    // dropped fill would leave the closed loop below waiting on a sample that
    // never comes, so let the merger take the whole backlog first.
    for (std::size_t s = 0; s < runtime_t::shard_count(); ++s) {
        while (rt.egress_backlog(s) != 0) {
            if (a.direct)
                (void)poll_all();
            cpu_relax();
        }
    }
    const std::uint64_t tsc_idle = read_tsc();
    const std::uint64_t delivered = sink.fills.load(std::memory_order_relaxed);
    sink.stamping.store(true, std::memory_order_relaxed);

    // Phase 2: latency. Only these orders are stamped, so the histogram holds
    // only this phase's samples.
    constexpr std::uint64_t lat_pairs = 200'000;
    std::uint64_t lag_sum = 0;
    std::uint64_t lag_final = 0;
    if (a.load > 0.0) {
        // Open loop. The mean gap is the time the saturated pipeline took per
        // fill it delivered, backlog included, over the load. Each bid is
        // stamped with its scheduled arrival, so a pair sent late behind a
        // stalled ring counts its wait, as the open loop of Treadmill does
        // (section II.A). A fixed seed repeats the schedule each run.
        if (delivered == 0)
            throw std::runtime_error("the throughput phase delivered no fill to size --load on");
        const double mean_gap =
            static_cast<double>(tsc_idle - tsc0) / static_cast<double>(delivered) / a.load;
        std::mt19937_64 gen{a.seed};
        std::exponential_distribution<double> gap{1.0 / mean_gap};
        // The schedule is drawn before the phase, so the send loop does no
        // arithmetic beyond its clock reads. A generator that falls behind its
        // schedule queues on the client side and biases the latency it
        // reports (Treadmill, section II.A), so the loop sums how late each
        // pair left, and the report prints the mean and the lag at the end.
        std::vector<std::uint64_t> due(lat_pairs);
        double at = 0.0;
        for (auto& d : due) {
            at += gap(gen);
            d = static_cast<std::uint64_t>(at);
        }
        const std::uint64_t start = read_tsc();
        for (std::uint64_t i = 0; i < lat_pairs; ++i) {
            const lob::symbol_id_t sym = i % num_symbols;
            const std::uint64_t t = start + due[i];
            std::uint64_t now = read_tsc();
            while (now < t) {
                if (a.direct)
                    (void)poll_all();
                cpu_relax();
                now = read_tsc();
            }
            lag_sum += now - t;
            while (!rt.try_submit(sym, ask(next++))) {
            }
            const lob::order_id_t bid_id = next++;
            send_tsc[bid_id & slot_mask].store(t, std::memory_order_relaxed);
            while (!rt.try_submit(sym, bid(bid_id))) {
            }
        }
        lag_final = read_tsc() - (start + due.back());
    } else {
        // Closed loop with one pair in flight, so the pipeline stays
        // unsaturated and the stamp-to-echo difference is processing latency,
        // not queueing.
        for (std::uint64_t i = 0; i < lat_pairs; ++i) {
            const lob::symbol_id_t sym = i % num_symbols;
            const std::uint64_t prev = sink.samples.load(std::memory_order_acquire);
            while (!rt.try_submit(sym, ask(next++))) {
            }
            const lob::order_id_t bid_id = next++;
            send_tsc[bid_id & slot_mask].store(read_tsc(), std::memory_order_relaxed);
            while (!rt.try_submit(sym, bid(bid_id))) {
            }
            while (sink.samples.load(std::memory_order_acquire) == prev) {
                if (a.direct)
                    (void)poll_all();
                cpu_relax();
            }
        }
    }

    rt.drain();
    if (a.direct) {
        // Whatever the workers published after the last poll still has to be
        // taken before the run reports, or the event count understates.
        while (poll_all() != 0) {
        }
    } else {
        merger.stop();
    }
    rt.stop();

    std::printf("throughput: orders=%llu  wall=%.3fs  %.2f Morders/s\n",
                static_cast<unsigned long long>(submitted), secs,
                static_cast<double>(submitted) / secs / 1e6);
    std::uint64_t dropped = 0;
    for (std::size_t s = 0; s < runtime_t::shard_count(); ++s) {
        dropped += rt.publisher(s).dropped();
    }
    std::printf("latency: %s samples=%llu  events=%llu  dropped=%llu\n",
                a.load > 0.0 ? "open-loop" : "unloaded round-trip",
                static_cast<unsigned long long>(sink.samples.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(sink.events),
                static_cast<unsigned long long>(dropped));
    std::printf("end-to-end latency (reference cycles): p50=%llu p99=%llu p99.9=%llu max=%llu\n",
                static_cast<unsigned long long>(hist.value_at_percentile(50.0)),
                static_cast<unsigned long long>(hist.value_at_percentile(99.0)),
                static_cast<unsigned long long>(hist.value_at_percentile(99.9)),
                static_cast<unsigned long long>(hist.max()));
    if (a.load > 0.0) {
        std::printf("generator lag (reference cycles): mean=%llu final=%llu\n",
                    static_cast<unsigned long long>(lag_sum / lat_pairs),
                    static_cast<unsigned long long>(lag_final));
    }
    if (hist.overflow_count() > 0) {
        std::printf(
            "latency WARNING: %llu samples exceeded the histogram range; "
            "max and upper percentiles understate the true tail\n",
            static_cast<unsigned long long>(hist.overflow_count()));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "lob_loadgen: %s\n", e.what());
        return 1;
    }
}
