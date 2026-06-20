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
// No market data is required. The flow is generated; the symbols spread across
// shards through the same SplitMix64 routing the runtime uses.

#include <lob/config.hpp>
#include <lob/egress_merger.hpp>
#include <lob/latency_histogram.hpp>
#include <lob/messages.hpp>
#include <lob/shard_egress_runtime.hpp>
#include <lob/types.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
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

[[nodiscard]] std::uint64_t now_tsc() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    return __builtin_ia32_rdtsc();
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

// Forwards every merged event and, for a taker fill whose submit stamp is
// parked, records the end-to-end latency. The latency phase stamps only the
// order in flight and waits on samples, so samples is atomic for the producer
// to observe across the merger thread. Satisfies the merge_sink concept.
struct latency_sink {
    std::vector<std::atomic<std::uint64_t>>& send_tsc;
    lob::latency_histogram& hist;
    std::uint64_t events{0};
    std::atomic<std::uint64_t> samples{0};

    void on_event(const lob::event& e, std::uint64_t /*merge_seq*/) noexcept {
        ++events;
        if (e.k == lob::event::kind::fill) {
            const auto t0 = send_tsc[e.body.fill.taker & slot_mask].load(std::memory_order_relaxed);
            if (t0 != 0) {
                hist.record(now_tsc() - t0);
                samples.fetch_add(1, std::memory_order_release);
            }
        }
    }
};

struct args {
    std::uint64_t orders{20'000'000};
    bool pin{false};
};

[[noreturn]] void usage(int code) {
    std::cerr << "usage: lob_loadgen [options]\n"
                 "  --orders N   total orders to submit (default 20000000)\n"
                 "  --pin        pin shard workers to cores\n"
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
        } else if (s == "--pin") {
            a.pin = true;
        } else if (s == "--help") {
            usage(0);
        } else {
            std::cerr << "unknown argument: " << s << "\n";
            usage(2);
        }
    }
    return a;
}

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

}  // namespace

int main(int argc, char** argv) {
    const args a = parse_args(argc, argv);

    std::vector<std::atomic<std::uint64_t>> send_tsc(slots);
    lob::latency_histogram hist{10'000'000, 3};

    const lob::shard_runtime_config rt_cfg{
        .pin_threads = a.pin, .first_core = 0, .core_stride = 1, .spin_budget = 1024};
    // The runtime embeds every shard's ingress and egress ring inline, tens of
    // megabytes in total, so it lives on the heap rather than the stack.
    const auto rt_ptr = std::make_unique<runtime_t>(lob::engine_config{}, rt_cfg);
    runtime_t& rt = *rt_ptr;
    latency_sink sink{send_tsc, hist};
    lob::egress_merger<runtime_t, latency_sink> merger{
        rt, sink, lob::merger_config{.pin_thread = false}};

    rt.start();
    merger.start();

    lob::order_id_t next = 1;

    // Phase 1: throughput. Fire every order at max rate without stamping, so
    // the saturated queueing delay is never mistaken for processing latency.
    const std::uint64_t pairs = a.orders / 2;
    std::uint64_t submitted = 0;
    const auto wall0 = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < pairs; ++i) {
        const lob::symbol_id_t sym = i % num_symbols;
        while (!rt.try_submit(sym, ask(next++))) {}
        while (!rt.try_submit(sym, bid(next++))) {}
        submitted += 2;
    }
    rt.drain();
    const auto wall1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(wall1 - wall0).count();

    // Phase 2: latency. Closed loop with one pair in flight, so the pipeline
    // stays unsaturated and the stamp-to-echo difference is processing latency,
    // not queueing. Only these orders are stamped, so the histogram holds only
    // unloaded samples.
    constexpr std::uint64_t lat_pairs = 200'000;
    for (std::uint64_t i = 0; i < lat_pairs; ++i) {
        const lob::symbol_id_t sym = i % num_symbols;
        const std::uint64_t prev = sink.samples.load(std::memory_order_acquire);
        while (!rt.try_submit(sym, ask(next++))) {}
        const lob::order_id_t bid_id = next++;
        send_tsc[bid_id & slot_mask].store(now_tsc(), std::memory_order_relaxed);
        while (!rt.try_submit(sym, bid(bid_id))) {}
        while (sink.samples.load(std::memory_order_acquire) == prev) {
            cpu_relax();
        }
    }

    rt.drain();
    merger.stop();
    rt.stop();

    std::printf("throughput: orders=%llu  wall=%.3fs  %.2f Morders/s\n",
                static_cast<unsigned long long>(submitted),
                secs,
                static_cast<double>(submitted) / secs / 1e6);
    std::printf("latency: unloaded round-trip samples=%llu  events=%llu\n",
                static_cast<unsigned long long>(sink.samples.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(sink.events));
    std::printf("end-to-end latency (reference cycles): p50=%llu p99=%llu p99.9=%llu max=%llu\n",
                static_cast<unsigned long long>(hist.value_at_percentile(50.0)),
                static_cast<unsigned long long>(hist.value_at_percentile(99.0)),
                static_cast<unsigned long long>(hist.value_at_percentile(99.9)),
                static_cast<unsigned long long>(hist.max()));
    return 0;
}
