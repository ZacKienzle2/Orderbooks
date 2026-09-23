// Model-based differential testing of the engine against the reference book.
//
// Claessen and Hughes' QuickCheck (doi:10.1145/351240.351266) makes two points
// this file depends on. A property states what must hold for every input, so
// the test says what the engine means rather than what one stream of commands
// happened to do; and when a property fails the framework shrinks the
// counterexample, so a failure arrives as the shortest command sequence that
// still breaks, not as the long stream that happened to contain it. Hughes'
// later account of QuickCheck on stateful systems
// (doi:10.1007/978-3-540-74130-5_9) adds the arrangement used here: model the
// system as a state machine, generate sequences of commands valid for the
// model, and check the implementation against the model after each one.
//
// The model is the small amount of state a command needs to be generated: the
// next id to hand out, the ids currently live, and the accounts already in
// use. The system under test is the pair of engines, fast and reference,
// driven with the same command; the property is that they publish the same
// events in the same order and end in the same book state.
//
// Nothing here fixes a magnitude by hand. Enumerations are enumerated by
// magic_enum, so a policy or a time-in-force added to the library is covered
// the day it is added rather than when someone remembers to extend a list.
// Sizes come from RapidCheck's own size parameter, which is what scales a
// property from small cases to large ones. The bounds that remain are the
// engine's own: the tick ladder it was instantiated with, the arena capacity
// it was given, and the batch a prefetch distance is measured against.

#include <lob/bitmap.hpp>
#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/shard_worker.hpp>
#include <lob/types.hpp>

#include "model_commands.hpp"
#include "recording_publisher.hpp"
#include "reference_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <rapidcheck/state.h>
#include <boost/pfr/core.hpp>
#include <boost/pfr/functions_for.hpp>
#include <boost/pfr/ops.hpp>
#include <magic_enum/magic_enum.hpp>

namespace {

namespace cmds = lob::test::cmds;

// The hierarchical bitmap indexes the ladder a machine word at a time, so a
// ladder of word_bits squared is the one where both of its levels are exactly
// full. That is the shape worth testing, and it is read off the word rather
// than chosen.
constexpr std::size_t word_bits = std::numeric_limits<std::uint64_t>::digits;
constexpr std::size_t ticks = word_bits * word_bits;

// An arena that cannot bind before the ladder does, so the properties about
// matching are not really about running out of orders.
constexpr std::size_t deep_arena = ticks;
// The smallest arena the slab permits. A book that cannot match, given one,
// rejects from its second resting order onward, so the reject path is reached
// by the shortest sequences RapidCheck generates rather than only by the
// longest.
constexpr std::size_t minimal_arena = 1;

using pub_t = lob::test::recording_publisher;
using ref_t = lob::test::reference_engine;

template <std::size_t MaxOrders>
using id_engine = lob::engine<pub_t, ticks, MaxOrders>;
using handle_engine = lob::engine<pub_t, ticks, deep_arena, lob::order_lookup::by_handle>;

// How commands reach the engine. apply_command is the one-at-a-time path a
// gateway uses; apply_batch is the path a shard worker drains a claim through,
// which runs the prefetch hints ahead of each command. Which one is in use is
// stated rather than inferred, because a default-constructed prefetch_plan is
// not an empty one and inferring it from the plan hid a harness bug behind a
// passing property.
enum class apply_path : std::uint8_t { command, batch };

// The two engines under one command stream, with the comparison that is the
// property itself.
//
// Commands can be held back and applied as a batch, because the prefetch hints
// read ids that earlier commands of the same batch may not have inserted yet.
template <class Engine>
struct diff_system {
    static constexpr bool by_handle = std::is_same_v<Engine, handle_engine>;

    pub_t pub{};
    Engine fast;
    ref_t ref;
    lob::prefetch_plan plan{};
    unsigned batch{1};
    apply_path path{apply_path::command};
    std::vector<lob::command> queued{};
    std::unordered_map<lob::order_id_t, lob::order_handle> handles{};
    // Events already compared, so each command is charged only with what it
    // published and a divergence names the command that caused it.
    std::size_t seen_fills{0};
    std::size_t seen_tops{0};
    std::size_t seen_trades{0};
    std::size_t seen_self_trades{0};
    std::size_t seen_rejects{0};

    diff_system(lob::engine_config cfg,
                std::size_t ref_max,
                lob::prefetch_plan p,
                unsigned b,
                apply_path ap)
        : fast{pub, cfg}, ref{cfg, ref_max}, plan{p}, batch{b}, path{ap} {}

    void push(lob::command c) {
        if constexpr (by_handle) {
            // Without an id index the handle is the only way to reach an order,
            // so every cancel and modify carries the one its order last
            // returned. Stale handles are the point: filled and cancelled
            // orders leave theirs behind while the arena reuses their slots.
            if (c.k == lob::command::kind::cancel)
                c.body.cancel.handle = handles[c.body.cancel.id];
            else if (c.k == lob::command::kind::modify)
                c.body.modify.handle = handles[c.body.modify.id];
        }
        queued.push_back(c);
        if (queued.size() >= batch)
            flush();
    }

    void flush() {
        if (queued.empty())
            return;
        const auto at = [this](unsigned i) noexcept -> const lob::command& { return queued[i]; };
        if (path == apply_path::command) {
            for (const auto& c : queued)
                record_handle_(c, lob::apply_command(fast, c));
        } else {
            // The batch reports each command's handle as it retires, which is
            // how a handle-mode client learns the handle to cancel by.
            lob::apply_batch(
                fast, static_cast<unsigned>(queued.size()), at, plan,
                [this](unsigned i, lob::order_handle h) noexcept { record_handle_(queued[i], h); });
        }
        for (const auto& c : queued)
            lob::apply_command(ref, c);
        queued.clear();
    }

    void record_handle_(const lob::command& c, lob::order_handle h) {
        if constexpr (by_handle) {
            if (c.k == lob::command::kind::submit)
                handles[c.body.submit.id] = h;
            else if (c.k == lob::command::kind::modify)
                handles[c.body.modify.new_id != 0 ? c.body.modify.new_id : c.body.modify.id] = h;
        } else {
            (void)c;
            (void)h;
        }
    }

    // Compares what the two engines published since the last call. Held-back
    // commands publish nothing yet, so a batched run compares once per flush.
    //
    // The events are aggregates, so Boost.PFR compares them member by member
    // without this file naming a single field. A field added to an event is
    // covered the moment it exists, where a hand-written comparison would go
    // on passing while ignoring it.
    template <class T>
    static void compare_tail(const std::vector<T>& fast_side,
                             const std::vector<T>& ref_side,
                             std::size_t& seen) {
        RC_ASSERT(fast_side.size() == ref_side.size());
        for (std::size_t i = seen; i < fast_side.size(); ++i)
            RC_ASSERT(boost::pfr::eq(fast_side[i], ref_side[i]));
        seen = fast_side.size();
    }

    void check(const lob::command&) { check_published(); }

    // Everything that can only be true once the sequence is over: a part-full
    // batch still to drain, and the books themselves.
    void finish() {
        flush();
        check_published();
        check_book();
        // QuickCheck's classify: report how often a sequence reached the seams
        // that matter, so a property that never rejects or never self-crosses
        // is visible as such rather than passing quietly.
        RC_CLASSIFY(!pub.fills.empty(), "matched");
        RC_CLASSIFY(!pub.self_trades.empty(), "dispatched a self cross");
        RC_CLASSIFY(!pub.rejects.empty(), "reached the arena limit");
    }

    void check_published() {
        compare_tail(pub.fills, ref.fills, seen_fills);
        compare_tail(pub.trades, ref.trades, seen_trades);
        compare_tail(pub.self_trades, ref.self_trades, seen_self_trades);
        compare_tail(pub.rejects, ref.rejects, seen_rejects);
        compare_tail(pub.tops, ref.tops, seen_tops);
    }

    // The books themselves, once the whole sequence has run. Equal event
    // streams do not by themselves prove equal resting state.
    void check_book() const {
        RC_ASSERT(fast.book_view().bids().best() == ref.best_bid());
        RC_ASSERT(fast.book_view().asks().best() == ref.best_ask());
        for (lob::tick_t px = 0; px < ticks; ++px) {
            RC_ASSERT(fast.book_view().bids().aggregate_at(px) ==
                      ref.aggregate(lob::side::bid, px));
            RC_ASSERT(fast.book_view().asks().aggregate_at(px) ==
                      ref.aggregate(lob::side::ask, px));
        }
    }
};

template <std::size_t MaxOrders>
struct id_system : diff_system<id_engine<MaxOrders>> {
    static constexpr std::size_t ticks = ::ticks;
    using diff_system<id_engine<MaxOrders>>::diff_system;
};

// The side a rest_cmd takes is a property of the system, so the command does
// not have to carry it and the book cannot cross itself.
template <std::size_t MaxOrders, lob::side Side>
struct one_sided_system : diff_system<id_engine<MaxOrders>> {
    static constexpr std::size_t ticks = ::ticks;
    static constexpr lob::side resting_side = Side;
    using diff_system<id_engine<MaxOrders>>::diff_system;
};

struct handle_system : diff_system<handle_engine> {
    static constexpr std::size_t ticks = ::ticks;
    using diff_system<handle_engine>::diff_system;
};

}  // namespace

TEST_CASE("engine matches the reference book under every self-cross policy",
          "[engine][differential][model]") {
    // magic_enum enumerates the policies, so a policy added to the library is
    // covered here without this file being edited.
    for (const auto policy : magic_enum::enum_values<lob::self_cross_policy>()) {
        rc::prop(std::string{"policy "} + std::string{magic_enum::enum_name(policy)}, [policy] {
            cmds::check_sequence<id_system<deep_arena>, cmds::submit, cmds::cancel, cmds::modify>(
                lob::engine_config{.self_cross = policy}, deep_arena, lob::prefetch_plan{}, 1U,
                apply_path::command);
        });
    }
}

TEST_CASE("engine matches the reference book at arena capacity",
          "[engine][differential][model][reject]") {
    // A book with orders on one side only never matches, so the population
    // only grows and both engines must reject the same residuals with the same
    // stamps once the arena is full.
    rc::prop("bids only, until the arena is full", [] {
        cmds::check_sequence<one_sided_system<minimal_arena, lob::side::bid>, cmds::rest,
                             cmds::cancel>(lob::engine_config{}, minimal_arena,
                                           lob::prefetch_plan{}, 1U, apply_path::command);
    });

    rc::prop("asks only, until the arena is full", [] {
        cmds::check_sequence<one_sided_system<minimal_arena, lob::side::ask>, cmds::rest,
                             cmds::cancel>(lob::engine_config{}, minimal_arena,
                                           lob::prefetch_plan{}, 1U, apply_path::command);
    });
}

TEST_CASE("prefetching batch drain matches the reference book",
          "[engine][differential][model][prefetch]") {
    // The hints read and never write, so a drain through apply_batch has to
    // match at every distance and batch size, including where a hint looks up
    // an id that an earlier command of the same batch has yet to insert or has
    // just erased. A distance is only meaningful against the batch it looks
    // ahead within, so the batch is drawn first and bounds the distances.
    rc::prop("batched application agrees at every prefetch distance", [] {
        const auto batch = *rc::gen::positive<unsigned>();
        const lob::prefetch_plan plan{
            .index_ahead = *rc::gen::inRange<unsigned>(0, batch + 1),
            .order_ahead = *rc::gen::inRange<unsigned>(0, batch + 1),
        };
        cmds::check_sequence<id_system<deep_arena>, cmds::submit, cmds::cancel, cmds::modify>(
            lob::engine_config{.self_cross = cmds::gen_enum<lob::self_cross_policy>()}, deep_arena,
            plan, batch, apply_path::batch);
    });
}

TEST_CASE("handle-driven engine matches the reference book",
          "[engine][differential][model][handle]") {
    rc::prop("handles reach the same orders an id index would", [] {
        cmds::check_sequence<handle_system, cmds::submit, cmds::cancel, cmds::modify>(
            lob::engine_config{.self_cross = cmds::gen_enum<lob::self_cross_policy>()}, deep_arena,
            lob::prefetch_plan{}, 1U, apply_path::command);
    });
}
