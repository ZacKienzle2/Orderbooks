// Invariants that hold of the engine alone, without a reference book to
// compare against: quantity is conserved, a fill-or-kill executes all or
// nothing, and the ladder, its cached aggregates and its bitmap stay in step.
//
// Each is stated as a property over generated command sequences (see
// tests/model_commands.hpp), so a violation arrives shrunk to the shortest
// sequence that shows it. The two examples at the end are examples on purpose:
// they pin one exact event down to its sequence number, which a property
// covers in general but does not spell out.

#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/shard_worker.hpp>
#include <lob/types.hpp>

#include "model_commands.hpp"
#include "recording_publisher.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <magic_enum/magic_enum.hpp>

namespace {

namespace cmds = lob::test::cmds;

// The ladder at which the hierarchical bitmap's two levels are exactly full,
// read off the machine word it indexes with rather than chosen.
constexpr std::size_t word_bits = std::numeric_limits<std::uint64_t>::digits;
constexpr std::size_t ladder = word_bits * word_bits;

using pub_t = lob::test::recording_publisher;
using eng_t = lob::engine<pub_t, ladder, ladder>;

// One engine driven by a generated command sequence. What each property adds
// is check(), run after every command, and finish(), run after the sequence.
struct engine_system {
    static constexpr std::size_t ticks = ladder;

    pub_t pub{};
    eng_t eng;
    // Where each event stream stood before the command now being checked, so a
    // property can look at what that command published and nothing else.
    std::size_t fills_before{0};
    std::size_t self_trades_before{0};

    explicit engine_system(lob::engine_config cfg) : eng{pub, cfg} {}

    void push(const lob::command& c) {
        fills_before = pub.fills.size();
        self_trades_before = pub.self_trades.size();
        lob::apply_command(eng, c);
    }

    [[nodiscard]] lob::qty_t resting_total() const noexcept {
        lob::qty_t total = 0;
        for (lob::tick_t px = 0; px < ticks; ++px) {
            total += eng.book_view().bids().aggregate_at(px);
            total += eng.book_view().asks().aggregate_at(px);
        }
        return total;
    }

    [[nodiscard]] lob::qty_t remaining_of(lob::order_id_t id) noexcept {
        auto* o = const_cast<lob::id_index&>(eng.book_view().index()).lookup(id);  // NOLINT
        return (o == nullptr) ? 0 : o->remaining;
    }
};

// Quantity is conserved across a stream that only rests and cancels:
//
//     sum(submitted) == 2 * sum(filled) + sum(cancelled) + resting + rejected
//
// A fill publishes one event of quantity Q but consumes Q from the aggressor
// and Q from the maker, hence the factor of two. A cancel's contribution is
// what the order had left at the moment it was cancelled, so the system reads
// that before the command is applied.
struct conservation_system : engine_system {
    lob::qty_t submitted{0};
    lob::qty_t cancelled{0};

    using engine_system::engine_system;

    void push(const lob::command& c) {
        if (c.k == lob::command::kind::submit)
            submitted += c.body.submit.qty;
        else if (c.k == lob::command::kind::cancel)
            cancelled += remaining_of(c.body.cancel.id);
        engine_system::push(c);
    }

    void check(const lob::command&) const noexcept {}

    void finish() const {
        lob::qty_t filled = 0;
        for (const auto& f : pub.fills)
            filled += f.qty;
        lob::qty_t rejected = 0;
        for (const auto& r : pub.rejects)
            rejected += r.qty;
        RC_ASSERT(submitted == (2 * filled) + cancelled + resting_total() + rejected);
        RC_CLASSIFY(filled > 0, "matched");
    }
};

// Fill-or-kill is all-or-nothing by definition: what a fill-or-kill aggressor
// executes, counting both the fills it takes and the self-trade netting it
// initiates under decrement_trade, is either nothing or the whole order. This
// is the class of defect where a precheck misjudges what a self-crossing
// fill-or-kill can actually consume, rather than any one instance of it.
struct fok_system : engine_system {
    using engine_system::engine_system;

    void check(const lob::command& c) const {
        if (c.k != lob::command::kind::submit || c.body.submit.t != lob::tif::fok)
            return;
        const auto id = c.body.submit.id;
        lob::qty_t executed = 0;
        for (auto i = fills_before; i < pub.fills.size(); ++i) {
            if (pub.fills[i].taker == id)
                executed += pub.fills[i].qty;
        }
        for (auto i = self_trades_before; i < pub.self_trades.size(); ++i) {
            if (pub.self_trades[i].aggressor == id)
                executed += pub.self_trades[i].qty;
        }
        RC_ASSERT((executed == 0 || executed == c.body.submit.qty));
    }

    void finish() const {
        RC_CLASSIFY(!pub.fills.empty(), "matched");
        RC_CLASSIFY(!pub.self_trades.empty(), "dispatched a self cross");
    }
};

// The cached level aggregate equals the sum of the remaining quantities in
// that level's FIFO, and a level holding quantity is a level the bitmap
// reports. Checked after every command, because a structure that is only
// consistent at the end of a stream is not consistent.
struct consistency_system : engine_system {
    using engine_system::engine_system;

    void check(const lob::command&) const {
        for (lob::tick_t px = 0; px < ticks; ++px) {
            check_side_(eng.book_view().bids(), px);
            check_side_(eng.book_view().asks(), px);
        }
    }

    void finish() const { RC_CLASSIFY(!pub.fills.empty(), "matched"); }

   private:
    template <class Side>
    static void check_side_(const Side& book, lob::tick_t px) {
        lob::qty_t sum = 0;
        for (const auto& o : book.level_at(px).fifo)
            sum += o.remaining;
        RC_ASSERT(book.aggregate_at(px) == sum);
        // The bitmap is the index over the ladder: an empty level is never the
        // best price, and a level with quantity is never past the best one.
        const auto best = book.best();
        if (sum == 0)
            RC_ASSERT(!best.has_value() || *best != px);
        else
            RC_ASSERT(best.has_value());
    }
};

}  // namespace

TEST_CASE("engine conserves quantity across a resting stream", "[engine][invariant][model]") {
    rc::prop("submitted equals filled twice over, plus cancelled, resting and rejected", [] {
        cmds::check_sequence<conservation_system, cmds::rest, cmds::cancel>(lob::engine_config{});
    });
}

TEST_CASE("engine fill-or-kill executes all or nothing", "[engine][invariant][model][fok]") {
    // magic_enum enumerates the policies, so a policy added to the library is
    // covered here without this file being edited.
    for (const auto policy : magic_enum::enum_values<lob::self_cross_policy>()) {
        rc::prop(std::string{"policy "} + std::string{magic_enum::enum_name(policy)}, [policy] {
            cmds::check_sequence<fok_system, cmds::submit, cmds::cancel, cmds::modify>(
                lob::engine_config{.self_cross = policy});
        });
    }
}

TEST_CASE("engine keeps its aggregates and bitmap in step with its FIFOs",
          "[engine][invariant][model]") {
    rc::prop("every level agrees with itself after every command", [] {
        cmds::check_sequence<consistency_system, cmds::submit, cmds::cancel, cmds::modify>(
            lob::engine_config{.self_cross = cmds::gen_enum<lob::self_cross_policy>()});
    });
}

TEST_CASE("engine publishes a reject when the arena is exhausted", "[engine][invariant][reject]") {
    // An example rather than a property: the property above establishes that
    // rejects agree with the reference book, and this pins the one event down
    // to its field values and its sequence number.
    constexpr std::size_t small_cap = 8;
    constexpr lob::tick_t px = 10;
    constexpr lob::qty_t resting_qty = 5;
    constexpr lob::account_id_t account = 3;

    pub_t pub;
    lob::engine<pub_t, ladder, small_cap> eng{pub, lob::engine_config{}};

    for (lob::order_id_t id = 1; id <= small_cap; ++id) {
        eng.on_submit({.id = id,
                       .px = px,
                       .qty = resting_qty,
                       .s = lob::side::bid,
                       .t = lob::tif::gtc,
                       ._pad = 0,
                       .account_id = account});
    }
    REQUIRE(pub.rejects.empty());
    const auto agg_before = eng.book_view().bids().aggregate_at(px);
    const auto seq_before = eng.last_seq();

    eng.on_submit({.id = 99,
                   .px = px,
                   .qty = 7,
                   .s = lob::side::bid,
                   .t = lob::tif::gtc,
                   ._pad = 0,
                   .account_id = account});

    REQUIRE(pub.rejects.size() == 1);
    CHECK(pub.rejects[0].id == 99);
    CHECK(pub.rejects[0].account == account);
    CHECK(pub.rejects[0].px == px);
    CHECK(pub.rejects[0].qty == 7);
    CHECK(pub.rejects[0].reason == lob::reject_reason::arena_full);
    CHECK(pub.rejects[0].seq == seq_before + 1);
    CHECK(eng.book_view().bids().aggregate_at(px) == agg_before);

    // A cancel frees a slot, so the next submit rests again with no reject.
    eng.on_cancel({.id = 1});
    eng.on_submit({.id = 100,
                   .px = px,
                   .qty = resting_qty,
                   .s = lob::side::bid,
                   .t = lob::tif::gtc,
                   ._pad = 0,
                   .account_id = account});
    REQUIRE(pub.rejects.size() == 1);
    CHECK(eng.book_view().bids().aggregate_at(px) == agg_before);
}
