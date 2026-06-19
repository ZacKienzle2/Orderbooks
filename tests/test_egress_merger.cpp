#include <lob/egress_merger.hpp>
#include <lob/messages.hpp>
#include <lob/shard_egress_runtime.hpp>
#include <lob/types.hpp>

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

}  // namespace

TEST_CASE("egress_merger forwards a crossing's events with a gap-free sequence", "[merger]") {
    runtime_t rt{lob::engine_config{}};
    recording_sink sink;
    sink.events.reserve(1024);
    sink.seqs.reserve(1024);
    lob::egress_merger<runtime_t, recording_sink> merger{rt, sink,
                                                         lob::merger_config{.pin_thread = false}};
    rt.start();
    merger.start();

    constexpr lob::symbol_id_t sym = 123;
    while (!rt.try_submit(sym, sub(1, 100, 10, lob::side::ask))) {
    }
    while (!rt.try_submit(sym, sub(2, 100, 4, lob::side::bid))) {
    }

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

TEST_CASE("egress_merger delivers every event across shards exactly once", "[merger]") {
    runtime_t rt{lob::engine_config{}};
    recording_sink sink;
    sink.events.reserve(8192);
    sink.seqs.reserve(8192);
    lob::egress_merger<runtime_t, recording_sink> merger{rt, sink,
                                                         lob::merger_config{.pin_thread = false}};
    rt.start();
    merger.start();

    for (lob::order_id_t i = 1; i <= 2'000; ++i) {
        const auto sym = static_cast<lob::symbol_id_t>(i % 16 + 1);
        const auto px = static_cast<lob::tick_t>(i % ticks);
        while (!rt.try_submit(sym, sub(i, px, 2, lob::side::bid))) {
        }
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
