#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

// Randomized torture stream. Drives a large mixed command stream through one
// engine and periodically asserts structural invariants, repeated under every
// self-cross policy and both throttle settings. Under the sanitizer presets this
// doubles as a memory and undefined-behaviour soak. The op count is kept modest
// so the sanitizer builds stay quick; a heavier offline run lives outside CI.

namespace {

constexpr std::size_t ticks = 512;
constexpr std::size_t max_ord = std::size_t{1} << 13;

struct counting_pub {
    void publish(const lob::fill_msg&) noexcept {}

    void publish(const lob::top_msg&) noexcept {}

    void publish(const lob::trade_msg&) noexcept {}

    void publish(const lob::self_trade_msg&) noexcept {}
};

using eng_t = lob::engine<counting_pub, ticks, max_ord>;

std::uint64_t splitmix(std::uint64_t& s) noexcept {
    std::uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

// Asserts, for one side over the whole ladder, that the bitmap's populated set
// equals the non-empty levels, each level's aggregate equals its FIFO sum, no
// resting order has zero remaining or a stray price, and best() is the extreme
// populated level.
template <lob::side S>
void audit_side(const lob::book_side<ticks, S>& sd) {
    std::vector<lob::tick_t> reality;
    for (lob::tick_t px = 0; px < ticks; ++px) {
        const auto& lvl = sd.level_at(px);
        lob::qty_t sum = 0;
        for (const auto& o : lvl.fifo) {
            REQUIRE(o.remaining > 0);
            REQUIRE(o.px == px);
            sum += o.remaining;
        }
        if (!lvl.fifo.empty()) {
            REQUIRE(sum == lvl.aggregate);
            reality.push_back(px);
        } else {
            REQUIRE(lvl.aggregate == 0);
        }
    }
    // Walk the bitmap into the same shape as reality. A signed sentinel keeps
    // the optional unwrap inside a checked ternary, so the loop body never
    // dereferences a bare optional.
    const auto next_pop = [&sd](lob::tick_t from) {
        const auto o = sd.next_populated_at_or_after(from);
        return o.has_value() ? static_cast<long>(*o) : -1L;
    };
    std::vector<lob::tick_t> bitmap;
    for (long v = next_pop(0); v >= 0; v = (v + 1 >= static_cast<long>(ticks))
                                               ? -1L
                                               : next_pop(static_cast<lob::tick_t>(v + 1))) {
        bitmap.push_back(static_cast<lob::tick_t>(v));
    }
    REQUIRE(bitmap == reality);
    const auto best = sd.best();
    if (reality.empty()) {
        REQUIRE_FALSE(best.has_value());
    } else {
        const lob::tick_t want = (S == lob::side::bid) ? reality.back() : reality.front();
        REQUIRE(best == want);
    }
}

void audit(const eng_t& eng) {
    audit_side<lob::side::bid>(eng.book_view().bids());
    audit_side<lob::side::ask>(eng.book_view().asks());
}

}  // namespace

TEST_CASE("engine holds its invariants under a randomized torture stream", "[engine][stress]") {
    const auto policy = GENERATE(lob::self_cross_policy::cancel_newest,
                                 lob::self_cross_policy::cancel_oldest,
                                 lob::self_cross_policy::decrement_trade);
    const bool throttle = GENERATE(true, false);

    constexpr std::uint64_t ops = 120'000;
    constexpr std::uint64_t audit_every = 20'000;

    counting_pub pub;
    eng_t eng{pub,
              lob::engine_config{.tick_size = 1,
                                 .max_order_qty = 1ULL << 32,
                                 .self_cross = policy,
                                 .top_throttle = throttle}};
    std::uint64_t rng =
        0xC0FFEEULL + static_cast<std::uint64_t>(policy) * 0x1000193ULL + (throttle ? 1ULL : 0ULL);
    std::vector<lob::order_id_t> live;
    live.reserve(max_ord);
    lob::order_id_t next = 1;

    for (std::uint64_t i = 0; i < ops; ++i) {
        const auto r = splitmix(rng) % 100;
        if (r < 35 && live.size() < max_ord - 64) {
            const auto pick = splitmix(rng);
            const auto tp = pick % 10;
            lob::tif t = lob::tif::gtc;
            if (tp >= 9) {
                t = lob::tif::fok;
            } else if (tp >= 7) {
                t = lob::tif::ioc;
            }
            const lob::submit_msg m{.id = next++,
                                    .px = static_cast<lob::tick_t>(1 + splitmix(rng) % (ticks - 2)),
                                    .qty = 1 + splitmix(rng) % 100,
                                    .s = (pick & 1U) != 0 ? lob::side::bid : lob::side::ask,
                                    .t = t,
                                    ._pad = 0,
                                    .account_id =
                                        static_cast<lob::account_id_t>(splitmix(rng) % 6)};
            eng.on_submit(m);
            if (m.t == lob::tif::gtc) {
                live.push_back(m.id);
            }
        } else if (r < 65 && !live.empty()) {
            const std::size_t k = splitmix(rng) % live.size();
            eng.on_cancel(lob::cancel_msg{.id = live[k]});
            live[k] = live.back();
            live.pop_back();
        } else if (r < 80 && !live.empty()) {
            const auto id = live[splitmix(rng) % live.size()];
            eng.on_modify(
                lob::modify_msg{.id = id,
                                .new_px = static_cast<lob::tick_t>(1 + splitmix(rng) % (ticks - 2)),
                                .new_qty = 1 + splitmix(rng) % 100});
        } else {
            const bool bid = (splitmix(rng) & 1U) != 0;
            eng.on_submit(
                lob::submit_msg{.id = next++,
                                .px = static_cast<lob::tick_t>(bid ? ticks - 2 : 1),
                                .qty = 1 + splitmix(rng) % 50,
                                .s = bid ? lob::side::bid : lob::side::ask,
                                .t = lob::tif::ioc,
                                ._pad = 0,
                                .account_id = static_cast<lob::account_id_t>(splitmix(rng) % 6)});
        }
        if ((i + 1) % audit_every == 0) {
            audit(eng);
        }
    }
    audit(eng);
}
