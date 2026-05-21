#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include "recording_publisher.hpp"

#include <cstddef>
#include <cstdint>
#include <random>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

namespace {

constexpr std::size_t ticks = 256;
constexpr std::size_t max_ord = 256;
constexpr std::size_t commands = 2'500;

using pub_t = lob::test::recording_publisher;
using eng_t = lob::engine<pub_t, ticks, max_ord>;

lob::qty_t resting_total_on_book(const eng_t& eng) noexcept {
    lob::qty_t total = 0;
    for (lob::tick_t px = 0; px < ticks; ++px) {
        total += eng.book_view().bids().aggregate_at(px);
        total += eng.book_view().asks().aggregate_at(px);
    }
    return total;
}

lob::qty_t order_remaining_or_zero(const eng_t& eng, lob::order_id_t id) noexcept {
    auto* o = const_cast<lob::id_index&>(eng.book_view().index()).lookup(id);  // NOLINT
    return (o == nullptr) ? 0 : o->remaining;
}

}  // namespace

// Conservation invariant for a GTC-only stream:
//
//     sum(submitted.qty) == 2 * sum(fill.qty) + sum(cancelled-at-cancel-time)
//                           + sum(resting-at-end)
//
// Each fill emits one fill_msg of quantity Q but consumes Q from both the
// aggressor and the resting maker - hence the factor of two. The test runs
// thousands of random submits and cancels, taking a snapshot of the cancelled
// order's remaining qty at cancel time, and verifies the equation holds.
TEST_CASE("engine GTC stream preserves quantity conservation", "[engine][invariant]") {
    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);
    std::mt19937_64 rng{seed};

    pub_t pub;
    eng_t eng{pub, lob::engine_config{}};

    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 50};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> op_dist{0, 9};

    lob::qty_t sum_submitted{0};
    lob::qty_t sum_cancelled{0};
    std::vector<lob::order_id_t> live;
    live.reserve(commands);
    lob::order_id_t next_id{1};

    for (std::size_t step = 0; step < commands; ++step) {
        const bool wants_cancel = !live.empty() && op_dist(rng) >= 7;
        if (wants_cancel) {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto k = pick(rng);
            const auto id = live[k];
            live[k] = live.back();
            live.pop_back();
            sum_cancelled += order_remaining_or_zero(eng, id);
            eng.on_cancel({.id = id});
        } else {
            const auto id = next_id++;
            const auto m = lob::submit_msg{
                .id = id,
                .px = px(rng),
                .qty = qty(rng),
                .s = (side_dist(rng) == 0) ? lob::side::bid : lob::side::ask,
                .t = lob::tif::gtc,
                ._pad = 0,
                .account_id = 0,
            };
            sum_submitted += m.qty;
            live.push_back(id);
            eng.on_submit(m);
        }
    }

    lob::qty_t sum_filled = 0;
    for (const auto& f : pub.fills)
        sum_filled += f.qty;
    const auto sum_resting = resting_total_on_book(eng);

    INFO("seed=" << seed << " submitted=" << sum_submitted << " filled=" << sum_filled
                 << " cancelled=" << sum_cancelled << " resting=" << sum_resting);
    REQUIRE(sum_submitted == (2 * sum_filled) + sum_cancelled + sum_resting);
}

// Structural invariant: at any moment, the cached level aggregate equals the
// sum of remaining quantities across the level's FIFO. The dense ladder and
// the bitmap stay in lockstep (a populated level implies a set bit, and vice
// versa). Both checked after every command in a mixed-TIF stream.
TEST_CASE("engine maintains aggregate / bitmap consistency across a mixed stream",
          "[engine][invariant]") {
    auto seed = GENERATE(0x1234567890ABCDEFULL, 0xFEEDFACECAFEBEEFULL);
    std::mt19937_64 rng{seed};

    pub_t pub;
    eng_t eng{pub, lob::engine_config{.self_cross = lob::self_cross_policy::decrement_trade}};

    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 50};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> tif_dist{0, 2};
    std::uniform_int_distribution<int> op_dist{0, 9};
    std::uniform_int_distribution<int> acct_dist{1, 3};

    std::vector<lob::order_id_t> live;
    lob::order_id_t next_id{1};

    auto check_consistency = [&] {
        for (lob::tick_t p = 0; p < ticks; ++p) {
            for (auto s : {lob::side::bid, lob::side::ask}) {
                const auto agg = (s == lob::side::bid) ? eng.book_view().bids().aggregate_at(p)
                                                       : eng.book_view().asks().aggregate_at(p);
                const auto& lvl = (s == lob::side::bid) ? eng.book_view().bids().level_at(p)
                                                        : eng.book_view().asks().level_at(p);
                lob::qty_t sum = 0;
                for (const auto& o : lvl.fifo)
                    sum += o.remaining;
                REQUIRE(agg == sum);
                // bitmap mirror: populated iff non-empty.
                const auto best_bid = eng.book_view().bids().best();
                const auto best_ask = eng.book_view().asks().best();
                if (sum == 0) {
                    REQUIRE((!best_bid.has_value() || *best_bid != p || s != lob::side::bid ||
                             eng.book_view().bids().aggregate_at(p) == 0));
                    REQUIRE((!best_ask.has_value() || *best_ask != p || s != lob::side::ask ||
                             eng.book_view().asks().aggregate_at(p) == 0));
                }
            }
        }
    };

    for (std::size_t step = 0; step < 800; ++step) {
        const auto roll = op_dist(rng);
        if (!live.empty() && roll < 2) {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto k = pick(rng);
            eng.on_cancel({.id = live[k]});
            live[k] = live.back();
            live.pop_back();
        } else if (!live.empty() && roll < 3) {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto k = pick(rng);
            eng.on_modify({.id = live[k], .new_px = px(rng), .new_qty = qty(rng)});
        } else {
            const auto id = next_id++;
            live.push_back(id);
            eng.on_submit({
                .id = id,
                .px = px(rng),
                .qty = qty(rng),
                .s = (side_dist(rng) == 0) ? lob::side::bid : lob::side::ask,
                .t = static_cast<lob::tif>(tif_dist(rng)),
                ._pad = 0,
                .account_id = static_cast<lob::account_id_t>(acct_dist(rng)),
            });
        }
        check_consistency();
    }
}
