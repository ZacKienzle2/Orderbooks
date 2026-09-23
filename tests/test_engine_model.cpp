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

struct model {
    lob::order_id_t next_id{1};
    std::vector<lob::order_id_t> live;
    // Accounts already used, so a generated order can join one rather than
    // always opening a new one. Self-crossing needs two orders to share an
    // account, and a generator that never reuses one would never reach it.
    std::vector<lob::account_id_t> accounts;
};

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

// Field generators. Each draws from the domain the type or the engine defines,
// and leaves magnitude to RapidCheck's size parameter.

lob::tick_t gen_price() {
    return *rc::gen::inRange<lob::tick_t>(0, ticks);
}

// Quantity is positive by the message's own contract; how large is the size
// parameter's business, which is what grows a property from small cases to
// large ones as it runs.
lob::qty_t gen_qty() {
    return *rc::gen::positive<lob::qty_t>();
}

template <class Enum>
Enum gen_enum() {
    return *rc::gen::elementOf(magic_enum::enum_values<Enum>());
}

// Either an account already trading or a new one, with no preference between
// them. Reuse is what puts two orders of one account on opposite sides, which
// is the only way to reach the self-cross policies at all.
lob::account_id_t gen_account(const model& m) {
    if (m.accounts.empty())
        return *rc::gen::arbitrary<lob::account_id_t>();
    return *rc::gen::oneOf(rc::gen::elementOf(m.accounts), rc::gen::arbitrary<lob::account_id_t>());
}

// Commands. Each is generated against the model state it will run in, applies
// its own effect to the model, and checks the two engines agree afterwards.

template <class Sys>
struct submit_cmd : rc::state::Command<model, Sys> {
    lob::side s{};
    lob::tick_t px{0};
    lob::qty_t qty{1};
    lob::tif t{};
    lob::account_id_t account{0};

    explicit submit_cmd(const model& m)
        : s{gen_enum<lob::side>()},
          px{gen_price()},
          qty{gen_qty()},
          t{gen_enum<lob::tif>()},
          account{gen_account(m)} {}

    void apply(model& m) const override {
        m.live.push_back(m.next_id);
        ++m.next_id;
        if (std::find(m.accounts.begin(), m.accounts.end(), account) == m.accounts.end())
            m.accounts.push_back(account);
    }

    void run(const model& m, Sys& sut) const override {
        sut.push(lob::command::make_submit(lob::submit_msg{
            .id = m.next_id,
            .px = px,
            .qty = qty,
            .s = s,
            .t = t,
            ._pad = 0,
            .account_id = account,
        }));
        sut.check_published();
    }

    void show(std::ostream& os) const override {
        os << "submit(px=" << px << ", qty=" << qty << ", side=" << magic_enum::enum_name(s)
           << ", tif=" << magic_enum::enum_name(t) << ", account=" << account << ")";
    }
};

// Submits that all take one side of the book, which therefore never cross and
// leave the arena as the only thing that can stop the book growing.
template <class Sys>
struct rest_cmd : rc::state::Command<model, Sys> {
    lob::tick_t px{0};
    lob::qty_t qty{1};

    explicit rest_cmd(const model&) : px{gen_price()}, qty{gen_qty()} {}

    void apply(model& m) const override {
        m.live.push_back(m.next_id);
        ++m.next_id;
    }

    void run(const model& m, Sys& sut) const override {
        sut.push(lob::command::make_submit(lob::submit_msg{
            .id = m.next_id,
            .px = px,
            .qty = qty,
            .s = Sys::resting_side,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0,
        }));
        sut.check_published();
    }

    void show(std::ostream& os) const override {
        os << "rest(px=" << px << ", qty=" << qty
           << ", side=" << magic_enum::enum_name(Sys::resting_side) << ")";
    }
};

template <class Sys>
struct cancel_cmd : rc::state::Command<model, Sys> {
    std::size_t k{0};

    explicit cancel_cmd(const model& m) {
        RC_PRE(!m.live.empty());
        k = *rc::gen::inRange<std::size_t>(0, m.live.size());
    }

    void checkPreconditions(const model& m) const override { RC_PRE(k < m.live.size()); }

    void apply(model& m) const override {
        m.live[k] = m.live.back();
        m.live.pop_back();
    }

    void run(const model& m, Sys& sut) const override {
        sut.push(lob::command::make_cancel(lob::cancel_msg{.id = m.live[k]}));
        sut.check_published();
    }

    void show(std::ostream& os) const override { os << "cancel(slot=" << k << ")"; }
};

template <class Sys>
struct modify_cmd : rc::state::Command<model, Sys> {
    std::size_t k{0};
    lob::tick_t px{0};
    lob::qty_t qty{1};
    bool rename{false};

    explicit modify_cmd(const model& m) {
        RC_PRE(!m.live.empty());
        k = *rc::gen::inRange<std::size_t>(0, m.live.size());
        px = gen_price();
        qty = gen_qty();
        // A cancel-replace renames the order, so the old id stops naming
        // anything and later commands must use the new one on both engines.
        rename = *rc::gen::arbitrary<bool>();
    }

    void checkPreconditions(const model& m) const override { RC_PRE(k < m.live.size()); }

    void apply(model& m) const override {
        if (rename) {
            m.live[k] = m.next_id;
            ++m.next_id;
        }
    }

    void run(const model& m, Sys& sut) const override {
        sut.push(lob::command::make_modify(lob::modify_msg{
            .id = m.live[k],
            .new_px = px,
            .new_qty = qty,
            .new_id = rename ? m.next_id : lob::order_id_t{0},
        }));
        sut.check_published();
    }

    void show(std::ostream& os) const override {
        os << "modify(slot=" << k << ", px=" << px << ", qty=" << qty
           << ", rename=" << (rename ? "yes" : "no") << ")";
    }
};

// Runs one generated command sequence and checks the two engines agree
// throughout it and at the end of it. The command set is a parameter because
// the capacity property needs a book that cannot match itself.
template <class Sys, template <class> class... Cmds>
void run_property(lob::engine_config cfg,
                  std::size_t ref_max,
                  lob::prefetch_plan plan,
                  unsigned batch,
                  apply_path path) {
    Sys sut{cfg, ref_max, plan, batch, path};
    rc::state::check(model{}, sut, rc::state::gen::execOneOfWithArgs<Cmds<Sys>...>());
    // A batch can end part-full, so drain it before the books are compared.
    sut.flush();
    sut.check_published();
    sut.check_book();
    // QuickCheck's classify: report how often a sequence reached the seams
    // that matter, so a property that never rejects or never self-crosses is
    // visible as such rather than passing quietly.
    RC_CLASSIFY(!sut.pub.fills.empty(), "matched");
    RC_CLASSIFY(!sut.pub.self_trades.empty(), "dispatched a self cross");
    RC_CLASSIFY(!sut.pub.rejects.empty(), "reached the arena limit");
}

template <std::size_t MaxOrders>
struct id_system : diff_system<id_engine<MaxOrders>> {
    using diff_system<id_engine<MaxOrders>>::diff_system;
};

// The side a rest_cmd takes is a property of the system, so the command does
// not have to carry it and the book cannot cross itself.
template <std::size_t MaxOrders, lob::side Side>
struct one_sided_system : diff_system<id_engine<MaxOrders>> {
    static constexpr lob::side resting_side = Side;
    using diff_system<id_engine<MaxOrders>>::diff_system;
};

struct handle_system : diff_system<handle_engine> {
    using diff_system<handle_engine>::diff_system;
};

}  // namespace

TEST_CASE("engine matches the reference book under every self-cross policy",
          "[engine][differential][model]") {
    // magic_enum enumerates the policies, so a policy added to the library is
    // covered here without this file being edited.
    for (const auto policy : magic_enum::enum_values<lob::self_cross_policy>()) {
        rc::prop(std::string{"policy "} + std::string{magic_enum::enum_name(policy)}, [policy] {
            run_property<id_system<deep_arena>, submit_cmd, cancel_cmd, modify_cmd>(
                lob::engine_config{.self_cross = policy}, deep_arena, lob::prefetch_plan{}, 1,
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
        run_property<one_sided_system<minimal_arena, lob::side::bid>, rest_cmd, cancel_cmd>(
            lob::engine_config{}, minimal_arena, lob::prefetch_plan{}, 1, apply_path::command);
    });

    rc::prop("asks only, until the arena is full", [] {
        run_property<one_sided_system<minimal_arena, lob::side::ask>, rest_cmd, cancel_cmd>(
            lob::engine_config{}, minimal_arena, lob::prefetch_plan{}, 1, apply_path::command);
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
        run_property<id_system<deep_arena>, submit_cmd, cancel_cmd, modify_cmd>(
            lob::engine_config{.self_cross = *rc::gen::elementOf(
                                   magic_enum::enum_values<lob::self_cross_policy>())},
            deep_arena, plan, batch, apply_path::batch);
    });
}

TEST_CASE("handle-driven engine matches the reference book",
          "[engine][differential][model][handle]") {
    rc::prop("handles reach the same orders an id index would", [] {
        run_property<handle_system, submit_cmd, cancel_cmd, modify_cmd>(
            lob::engine_config{.self_cross = *rc::gen::elementOf(
                                   magic_enum::enum_values<lob::self_cross_policy>())},
            deep_arena, lob::prefetch_plan{}, 1, apply_path::command);
    });
}
