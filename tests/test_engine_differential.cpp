#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/shard_worker.hpp>
#include <lob/types.hpp>

#include "recording_publisher.hpp"
#include "reference_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

namespace {

constexpr std::size_t ticks = 256;
constexpr std::size_t max_ord = 256;
constexpr std::size_t cmd_qty = 4'000;

using pub_t = lob::test::recording_publisher;
using fast_t = lob::engine<pub_t, ticks, max_ord>;
using handle_t = lob::engine<pub_t, ticks, max_ord, lob::order_lookup::by_handle>;
using ref_t = lob::test::reference_engine;

struct gen_state {
    std::mt19937_64 rng;
    lob::order_id_t next_id{1};
    std::vector<lob::order_id_t> live;
};

lob::submit_msg gen_submit(gen_state& g, std::uint8_t n_accounts) {
    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 50};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> tif_dist{0, 2};

    const auto id = g.next_id++;
    g.live.push_back(id);

    // n_accounts == 0 leaves account_id = 0, which the engine treats as
    // "no account tracking" and skips self-cross dispatch entirely.
    lob::account_id_t aid = 0;
    if (n_accounts > 0) {
        std::uniform_int_distribution<int> acct_dist{1, n_accounts};
        aid = static_cast<lob::account_id_t>(acct_dist(g.rng));
    }

    return {
        .id = id,
        .px = px(g.rng),
        .qty = qty(g.rng),
        .s = (side_dist(g.rng) == 0) ? lob::side::bid : lob::side::ask,
        .t = static_cast<lob::tif>(tif_dist(g.rng)),
        ._pad = 0,
        .account_id = aid,
    };
}

lob::cancel_msg gen_cancel(gen_state& g) {
    std::uniform_int_distribution<std::size_t> pick{0, g.live.size() - 1};
    const auto k = pick(g.rng);
    const auto id = g.live[k];
    g.live[k] = g.live.back();
    g.live.pop_back();
    return {.id = id};
}

lob::modify_msg gen_modify(gen_state& g) {
    std::uniform_int_distribution<std::size_t> pick{0, g.live.size() - 1};
    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 50};
    std::uniform_int_distribution<int> rename{0, 3};
    const auto k = pick(g.rng);
    const auto old_id = g.live[k];
    lob::order_id_t new_id = 0;
    // A quarter of modifies chain to a fresh id, cancel-replace style. The
    // old id stops naming anything, so later ops drawn from the live list
    // use the new one on both engines.
    if (rename(g.rng) == 0) {
        new_id = g.next_id++;
        g.live[k] = new_id;
    }
    return {.id = old_id, .new_px = px(g.rng), .new_qty = qty(g.rng), .new_id = new_id};
}

// One command from the mix every stream here draws, six submits, two cancels
// and two modifies in ten, and a submit whenever nothing is live.
lob::command gen_command(gen_state& g, std::uint8_t n_accounts) {
    std::uniform_int_distribution<int> op_dist{0, 9};
    const int roll = g.live.empty() ? 0 : op_dist(g.rng);
    if (roll < 6)
        return lob::command::make_submit(gen_submit(g, n_accounts));
    if (roll < 8)
        return lob::command::make_cancel(gen_cancel(g));
    return lob::command::make_modify(gen_modify(g));
}

void replay(fast_t& fast, ref_t& ref, gen_state& g, std::uint8_t n_accounts) {
    for (std::size_t i = 0; i < cmd_qty; ++i) {
        const auto c = gen_command(g, n_accounts);
        lob::apply_command(fast, c);
        lob::apply_command(ref, c);
    }
}

void compare_fills(const std::vector<lob::fill_msg>& a, const std::vector<lob::fill_msg>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].maker == b[i].maker);
        REQUIRE(a[i].taker == b[i].taker);
        REQUIRE(a[i].px == b[i].px);
        REQUIRE(a[i].qty == b[i].qty);
        REQUIRE(a[i].seq == b[i].seq);
    }
}

void compare_trades(const std::vector<lob::trade_msg>& a, const std::vector<lob::trade_msg>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].px == b[i].px);
        REQUIRE(a[i].qty == b[i].qty);
        REQUIRE(a[i].seq == b[i].seq);
    }
}

void compare_self_trades(const std::vector<lob::self_trade_msg>& a,
                         const std::vector<lob::self_trade_msg>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].aggressor == b[i].aggressor);
        REQUIRE(a[i].resting == b[i].resting);
        REQUIRE(a[i].account == b[i].account);
        REQUIRE(a[i].px == b[i].px);
        REQUIRE(a[i].qty == b[i].qty);
        REQUIRE(a[i].seq == b[i].seq);
    }
}

void compare_rejects(const std::vector<lob::reject_msg>& a, const std::vector<lob::reject_msg>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].id == b[i].id);
        REQUIRE(a[i].account == b[i].account);
        REQUIRE(a[i].px == b[i].px);
        REQUIRE(a[i].qty == b[i].qty);
        REQUIRE(a[i].reason == b[i].reason);
        REQUIRE(a[i].seq == b[i].seq);
    }
}

void compare_tops(const std::vector<lob::top_msg>& a, const std::vector<lob::top_msg>& b) {
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].bid_px == b[i].bid_px);
        REQUIRE(a[i].ask_px == b[i].ask_px);
        REQUIRE(a[i].bid_qty == b[i].bid_qty);
        REQUIRE(a[i].ask_qty == b[i].ask_qty);
        REQUIRE(a[i].seq == b[i].seq);
    }
}

template <class Engine>
void compare_book_state(const Engine& fast, const ref_t& ref) {
    REQUIRE(fast.book_view().bids().best() == ref.best_bid());
    REQUIRE(fast.book_view().asks().best() == ref.best_ask());
    for (lob::tick_t px = 0; px < ticks; ++px) {
        REQUIRE(fast.book_view().bids().aggregate_at(px) == ref.aggregate(lob::side::bid, px));
        REQUIRE(fast.book_view().asks().aggregate_at(px) == ref.aggregate(lob::side::ask, px));
    }
}

}  // namespace

TEST_CASE("engine matches reference on random streams (no self-cross)", "[engine][differential]") {
    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL, 0x1234567890ABCDEFULL);

    pub_t pub;
    fast_t fast{pub, lob::engine_config{}};
    ref_t ref{lob::engine_config{}, max_ord};

    gen_state g{.rng = std::mt19937_64{seed}, .next_id = 1, .live = {}};
    replay(fast, ref, g, /*n_accounts=*/0);  // account_id == 0 => engine skips self-cross dispatch

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_rejects(pub.rejects, ref.rejects);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}

TEST_CASE("engine matches reference under self_cross cancel_newest",
          "[engine][differential][self-cross]") {
    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);

    pub_t pub;
    fast_t fast{pub, lob::engine_config{.self_cross = lob::self_cross_policy::cancel_newest}};
    ref_t ref{lob::engine_config{.self_cross = lob::self_cross_policy::cancel_newest}, max_ord};

    gen_state g{.rng = std::mt19937_64{seed}, .next_id = 1, .live = {}};
    replay(fast, ref, g, /*n_accounts=*/4);

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_rejects(pub.rejects, ref.rejects);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}

TEST_CASE("engine matches reference under self_cross cancel_oldest",
          "[engine][differential][self-cross]") {
    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);

    pub_t pub;
    fast_t fast{pub, lob::engine_config{.self_cross = lob::self_cross_policy::cancel_oldest}};
    ref_t ref{lob::engine_config{.self_cross = lob::self_cross_policy::cancel_oldest}, max_ord};

    gen_state g{.rng = std::mt19937_64{seed}, .next_id = 1, .live = {}};
    replay(fast, ref, g, /*n_accounts=*/4);

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_rejects(pub.rejects, ref.rejects);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}

TEST_CASE("engine matches reference under self_cross decrement_trade",
          "[engine][differential][self-cross]") {
    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);

    pub_t pub;
    fast_t fast{pub, lob::engine_config{.self_cross = lob::self_cross_policy::decrement_trade}};
    ref_t ref{lob::engine_config{.self_cross = lob::self_cross_policy::decrement_trade}, max_ord};

    gen_state g{.rng = std::mt19937_64{seed}, .next_id = 1, .live = {}};
    replay(fast, ref, g, /*n_accounts=*/4);

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_rejects(pub.rejects, ref.rejects);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}

TEST_CASE("engine matches reference at arena capacity", "[engine][differential][reject]") {
    // Bids and asks in disjoint bands never cross, so the book only grows
    // and the resting population passes MaxOrders. Both engines must reject
    // the same residuals with the same stamps; the crossing mixes above
    // never exhaust the arena, so without this case the reject seam has no
    // differential coverage.
    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL);

    pub_t pub;
    fast_t fast{pub, lob::engine_config{}};
    ref_t ref{lob::engine_config{}, max_ord};

    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<lob::tick_t> bid_px{0, 100};
    std::uniform_int_distribution<lob::tick_t> ask_px{156, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 50};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> op_dist{0, 9};

    std::vector<lob::order_id_t> live;
    lob::order_id_t next_id = 1;
    for (std::size_t i = 0; i < 1'000; ++i) {
        if (!live.empty() && op_dist(rng) == 0) {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto k = pick(rng);
            const lob::cancel_msg m{.id = live[k]};
            live[k] = live.back();
            live.pop_back();
            fast.on_cancel(m);
            ref.on_cancel(m);
        } else {
            const bool is_bid = side_dist(rng) == 0;
            const lob::submit_msg m{
                .id = next_id++,
                .px = is_bid ? bid_px(rng) : ask_px(rng),
                .qty = qty(rng),
                .s = is_bid ? lob::side::bid : lob::side::ask,
                .t = lob::tif::gtc,
                ._pad = 0,
                .account_id = 0,
            };
            live.push_back(m.id);
            fast.on_submit(m);
            ref.on_submit(m);
        }
    }

    // The stream must actually have exhausted the arena, or this case
    // proves nothing.
    REQUIRE(pub.rejects.size() > 0);
    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_rejects(pub.rejects, ref.rejects);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}

TEST_CASE("prefetching batch drain matches reference", "[engine][differential][prefetch]") {
    // apply_batch runs engine::prefetch and engine::prefetch_order ahead of
    // each command. They read and never write, so a drain through it has to
    // match the reference at every distance and batch size, including where a
    // hint looks up an id that an earlier command of the same batch has yet
    // to insert or has just erased.
    const auto [index_ahead, order_ahead] =
        GENERATE(table<unsigned, unsigned>({{0, 0}, {2, 1}, {8, 4}}));
    const auto batch = GENERATE(1U, 7U, 64U);
    const auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL);
    const lob::prefetch_plan plan{.index_ahead = index_ahead, .order_ahead = order_ahead};
    const lob::engine_config cfg{.self_cross = lob::self_cross_policy::cancel_newest};

    pub_t pub;
    fast_t fast{pub, cfg};
    ref_t ref{cfg, max_ord};

    gen_state g{.rng = std::mt19937_64{seed}, .next_id = 1, .live = {}};
    std::vector<lob::command> cmds;
    cmds.reserve(batch);
    for (std::size_t done = 0; done < cmd_qty; done += cmds.size()) {
        cmds.clear();
        while (cmds.size() < batch && done + cmds.size() < cmd_qty)
            cmds.push_back(gen_command(g, /*n_accounts=*/4));
        const auto at = [&cmds](unsigned i) noexcept -> const lob::command& { return cmds[i]; };
        lob::apply_batch(fast, static_cast<unsigned>(cmds.size()), at, plan);
        for (const auto& c : cmds)
            lob::apply_command(ref, c);
    }

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_rejects(pub.rejects, ref.rejects);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}

TEST_CASE("handle-driven engine matches reference", "[engine][differential][handle]") {
    // Every cancel and modify carries the handle its order last returned, and
    // the engine keeps no index, so the handle is the only way to reach an
    // order. Orders that fill or cancel leave their handles stale while LIFO
    // reuse hands their slots to later orders, so the generation and id checks
    // run throughout.
    const auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);
    const lob::engine_config cfg{.self_cross = lob::self_cross_policy::cancel_newest};

    pub_t pub;
    handle_t fast{pub, cfg};
    ref_t ref{cfg, max_ord};

    gen_state g{.rng = std::mt19937_64{seed}, .next_id = 1, .live = {}};
    std::unordered_map<lob::order_id_t, lob::order_handle> handles;
    for (std::size_t i = 0; i < cmd_qty; ++i) {
        auto c = gen_command(g, /*n_accounts=*/4);
        if (c.k == lob::command::kind::cancel)
            c.body.cancel.handle = handles[c.body.cancel.id];
        else if (c.k == lob::command::kind::modify)
            c.body.modify.handle = handles[c.body.modify.id];
        const auto h = lob::apply_command(fast, c);
        lob::apply_command(ref, c);
        if (c.k == lob::command::kind::submit)
            handles[c.body.submit.id] = h;
        else if (c.k == lob::command::kind::modify)
            handles[c.body.modify.new_id != 0 ? c.body.modify.new_id : c.body.modify.id] = h;
    }

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_rejects(pub.rejects, ref.rejects);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}
