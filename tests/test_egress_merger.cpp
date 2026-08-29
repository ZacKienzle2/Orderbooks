#include <lob/egress_merger.hpp>
#include <lob/messages.hpp>
#include <lob/shard_egress_runtime.hpp>
#include <lob/types.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::size_t ticks = 128;
constexpr std::size_t max_ord = 256;
constexpr std::size_t shards = 4;
constexpr std::size_t ingress = 1024;
constexpr std::size_t egress = 4096;

using runtime_t = lob::shard_egress_runtime<ticks, max_ord, shards, ingress, egress>;

// Records the merged stream. Only the merger thread calls on_event, and the
// test reads the vectors after stop() joins that thread, so no lock is needed.
// The vectors are reserved up front because on_event is noexcept.
struct recording_sink {
    std::vector<lob::event> events;
    std::vector<std::uint64_t> seqs;

    void on_event(const lob::event& e, std::uint64_t seq) noexcept {
        events.push_back(e);
        seqs.push_back(seq);
    }
};

lob::submit_msg sub(lob::order_id_t id, lob::tick_t px, lob::qty_t qty, lob::side s) {
    return {.id = id, .px = px, .qty = qty, .s = s, .t = lob::tif::gtc, ._pad = 0, .account_id = 0};
}

// Deterministic two-shard source preloaded before the merger starts, so the
// merged order depends only on the merger's round schedule, never on producer
// timing. Events are trade_msg records tagged with (shard in px, index in
// qty) so the sink can reconstruct which shard each event came from.
struct preloaded_source {
    std::array<std::vector<lob::event>, 2> queues;
    std::array<std::size_t, 2> heads{};

    [[nodiscard]] bool try_poll(std::size_t s, lob::event& out) noexcept {
        if (heads[s] >= queues[s].size())
            return false;
        out = queues[s][heads[s]++];
        return true;
    }

    template <class F>
    [[nodiscard]] unsigned poll_batch(std::size_t s, unsigned max_n, F fn) noexcept {
        unsigned n = 0;
        while (n < max_n && heads[s] < queues[s].size()) {
            fn(queues[s][heads[s]++]);
            ++n;
        }
        return n;
    }

    [[nodiscard]] static constexpr std::size_t shard_count() noexcept { return 2; }
};

}  // namespace

TEST_CASE("egress_merger forwards a crossing's events with a gap-free sequence", "[merger]") {
    runtime_t rt{lob::engine_config{}};
    recording_sink sink;
    sink.events.reserve(1024);
    sink.seqs.reserve(1024);
    lob::egress_merger<runtime_t, recording_sink> merger{
        rt, sink, lob::merger_config{.pin_thread = false}};
    rt.start();
    merger.start();

    constexpr lob::symbol_id_t sym = 123;
    while (!rt.try_submit(sym, sub(1, 100, 10, lob::side::ask))) {}
    while (!rt.try_submit(sym, sub(2, 100, 4, lob::side::bid))) {}

    rt.drain();
    rt.stop();
    merger.stop();

    std::size_t fills = 0;
    for (const auto& e : sink.events) {
        if (e.k == lob::event::kind::fill) {
            ++fills;
            REQUIRE(e.body.fill.qty == 4);
        }
    }
    REQUIRE(fills == 1);
    REQUIRE(merger.merged() == sink.events.size());
    for (std::size_t i = 0; i < sink.seqs.size(); ++i) {
        REQUIRE(sink.seqs[i] == i);
    }
}

TEST_CASE("egress_merger interleaves backlogged shards in bounded batches", "[merger]") {
    constexpr unsigned batch_max = 4;
    constexpr std::size_t per_shard = 10;

    preloaded_source src;
    for (std::size_t s = 0; s < 2; ++s) {
        for (std::size_t i = 0; i < per_shard; ++i) {
            src.queues[s].push_back(lob::event::make_trade(lob::trade_msg{
                .px = static_cast<lob::tick_t>(s), .qty = static_cast<lob::qty_t>(i), .seq = 0}));
        }
    }

    recording_sink sink;
    sink.events.reserve(2 * per_shard);
    sink.seqs.reserve(2 * per_shard);
    lob::egress_merger<preloaded_source, recording_sink> merger{
        src, sink, lob::merger_config{.pin_thread = false, .batch_max = batch_max}};
    merger.start();
    merger.stop();

    REQUIRE(sink.events.size() == 2 * per_shard);
    // Both queues were full when the merger started, so every round claims
    // batch_max from shard 0 then batch_max from shard 1. A drain-to-empty
    // merger would instead emit all of shard 0 before any of shard 1.
    std::array<std::size_t, 2> next{};
    std::size_t i = 0;
    while (i < sink.events.size()) {
        for (std::size_t s = 0; s < 2; ++s) {
            const auto run = std::min<std::size_t>(batch_max, per_shard - next[s]);
            for (std::size_t j = 0; j < run; ++j, ++i) {
                REQUIRE(static_cast<std::size_t>(sink.events[i].body.trade.px) == s);
                REQUIRE(sink.events[i].body.trade.qty == next[s]);
                ++next[s];
            }
        }
    }
    REQUIRE(next[0] == per_shard);
    REQUIRE(next[1] == per_shard);
    for (std::size_t k = 0; k + 1 < sink.events.size(); ++k) {
        REQUIRE(sink.seqs[k + 1] == sink.seqs[k] + 1);
    }
}

TEST_CASE("egress_merger delivers every event across shards exactly once", "[merger]") {
    runtime_t rt{lob::engine_config{}};
    recording_sink sink;
    sink.events.reserve(8192);
    sink.seqs.reserve(8192);
    lob::egress_merger<runtime_t, recording_sink> merger{
        rt, sink, lob::merger_config{.pin_thread = false}};
    rt.start();
    merger.start();

    for (lob::order_id_t i = 1; i <= 2'000; ++i) {
        const lob::symbol_id_t sym = i % 16 + 1;
        const auto px = static_cast<lob::tick_t>(i % ticks);
        while (!rt.try_submit(sym, sub(i, px, 2, lob::side::bid))) {}
    }

    rt.drain();
    rt.stop();
    merger.stop();

    REQUIRE(sink.events.size() > 0);
    REQUIRE(merger.merged() == sink.events.size());
    for (std::size_t i = 0; i < sink.seqs.size(); ++i) {
        REQUIRE(sink.seqs[i] == i);
    }
}
