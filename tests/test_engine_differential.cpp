#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include "recording_publisher.hpp"
#include "reference_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

namespace {

constexpr std::size_t ticks = 256;
constexpr std::size_t max_ord = 256;
constexpr std::size_t cmd_qty = 4'000;

using pub_t = lob::test::recording_publisher;
using fast_t = lob::engine<pub_t, ticks, max_ord>;
using ref_t = lob::test::reference_engine;

enum class op_kind { submit, cancel, modify };

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
    return {.id = g.live[pick(g.rng)], .new_px = px(g.rng), .new_qty = qty(g.rng)};
}

void replay(fast_t& fast, ref_t& ref, gen_state& g, std::uint8_t n_accounts) {
    std::uniform_int_distribution<int> op_dist{0, 9};
    for (std::size_t i = 0; i < cmd_qty; ++i) {
        op_kind k;
        if (g.live.empty()) {
            k = op_kind::submit;
        } else {
            const auto roll = op_dist(g.rng);
            if (roll < 6)
                k = op_kind::submit;
            else if (roll < 8)
                k = op_kind::cancel;
            else
                k = op_kind::modify;
        }
        switch (k) {
        case op_kind::submit: {
            const auto m = gen_submit(g, n_accounts);
            fast.on_submit(m);
            ref.on_submit(m);
            break;
        }
        case op_kind::cancel: {
            const auto m = gen_cancel(g);
            fast.on_cancel(m);
            ref.on_cancel(m);
            break;
        }
        case op_kind::modify: {
            const auto m = gen_modify(g);
            fast.on_modify(m);
            ref.on_modify(m);
            break;
        }
        }
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

void compare_book_state(const fast_t& fast, const ref_t& ref) {
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
    ref_t ref{lob::engine_config{}};

    gen_state g{.rng = std::mt19937_64{seed}};
    replay(fast, ref, g, /*n_accounts=*/0);  // account_id == 0 => engine skips self-cross dispatch

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}

TEST_CASE("engine matches reference under self_cross cancel_newest",
          "[engine][differential][self-cross]") {
    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);

    pub_t pub;
    fast_t fast{pub, lob::engine_config{.self_cross = lob::self_cross_policy::cancel_newest}};
    ref_t ref{lob::engine_config{.self_cross = lob::self_cross_policy::cancel_newest}};

    gen_state g{.rng = std::mt19937_64{seed}};
    replay(fast, ref, g, /*n_accounts=*/4);

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}

TEST_CASE("engine matches reference under self_cross decrement_trade",
          "[engine][differential][self-cross]") {
    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);

    pub_t pub;
    fast_t fast{pub, lob::engine_config{.self_cross = lob::self_cross_policy::decrement_trade}};
    ref_t ref{lob::engine_config{.self_cross = lob::self_cross_policy::decrement_trade}};

    gen_state g{.rng = std::mt19937_64{seed}};
    replay(fast, ref, g, /*n_accounts=*/4);

    compare_fills(pub.fills, ref.fills);
    compare_trades(pub.trades, ref.trades);
    compare_self_trades(pub.self_trades, ref.self_trades);
    compare_tops(pub.tops, ref.tops);
    compare_book_state(fast, ref);
}
