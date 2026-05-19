#ifndef LOB_TESTS_REFERENCE_ENGINE_HPP
#define LOB_TESTS_REFERENCE_ENGINE_HPP

#include <lob/config.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lob::test {

// Reference matching engine. The implementation favours obvious correctness
// over performance: every level lives in a std::map keyed by price; every
// FIFO is a std::list of order records; the id index is a std::unordered_map.
//
// Behaviour mirrors lob::engine: strict price-time priority, GTC / IOC / FOK
// time-in-force, the same three self-cross policies. Outputs (fills, trades,
// self_trades, tops) are recorded in submission order with a monotonic seq.
//
// Used by the differential property test to validate the dense-ladder engine
// against a structurally different but semantically identical implementation.
struct reference_engine {
    struct rec {
        order_id_t id;
        qty_t remaining;
        tick_t px;
        side s;
        tif t;
        account_id_t account_id;
    };

    using bid_book = std::map<tick_t, std::list<rec>, std::greater<>>;
    using ask_book = std::map<tick_t, std::list<rec>, std::less<>>;

    bid_book bids;
    ask_book asks;
    std::unordered_map<order_id_t, rec*> idx;
    engine_config cfg;
    seq_t seq{0};
    std::vector<fill_msg> fills;
    std::vector<top_msg> tops;
    std::vector<trade_msg> trades;
    std::vector<self_trade_msg> self_trades;
    bool have_top{false};
    tick_t last_bid_px{0};
    tick_t last_ask_px{0};
    qty_t last_bid_qty{0};
    qty_t last_ask_qty{0};
    bool top_dirty{false};

    explicit reference_engine(engine_config c) noexcept : cfg{c} {}

    [[nodiscard]] std::optional<tick_t> best_bid() const noexcept {
        if (bids.empty())
            return std::nullopt;
        return bids.begin()->first;
    }

    [[nodiscard]] std::optional<tick_t> best_ask() const noexcept {
        if (asks.empty())
            return std::nullopt;
        return asks.begin()->first;
    }

    [[nodiscard]] qty_t aggregate(side s, tick_t px) const noexcept {
        if (s == side::bid) {
            auto it = bids.find(px);
            if (it == bids.end())
                return 0;
            qty_t sum = 0;
            for (const auto& r : it->second)
                sum += r.remaining;
            return sum;
        }
        auto it = asks.find(px);
        if (it == asks.end())
            return 0;
        qty_t sum = 0;
        for (const auto& r : it->second)
            sum += r.remaining;
        return sum;
    }

    void on_submit(const submit_msg& m) noexcept {
        if (m.s == side::bid)
            handle_<side::bid>(m);
        else
            handle_<side::ask>(m);
        publish_top_();
    }

    void on_cancel(const cancel_msg& m) noexcept {
        auto it = idx.find(m.id);
        if (it == idx.end())
            return;
        rec* r = it->second;
        idx.erase(it);
        if (r->s == side::bid)
            erase_from_<side::bid>(r);
        else
            erase_from_<side::ask>(r);
        top_dirty = true;
        publish_top_();
    }

    void on_modify(const modify_msg& m) noexcept {
        auto it = idx.find(m.id);
        if (it == idx.end())
            return;
        rec* r = it->second;
        const auto s = r->s;
        const auto t = r->t;
        const auto a = r->account_id;
        if (m.new_px == r->px) {
            if (m.new_qty == r->remaining)
                return;
            r->remaining = m.new_qty;
            top_dirty = true;
            publish_top_();
            return;
        }
        on_cancel(cancel_msg{.id = m.id});
        on_submit(submit_msg{.id = m.id,
                             .px = m.new_px,
                             .qty = m.new_qty,
                             .s = s,
                             .t = t,
                             ._pad = 0,
                             .account_id = a});
    }

  private:
    template <side Side>
    void handle_(const submit_msg& m) noexcept {
        if (m.t == tif::fok && !can_fully_fill_<Side>(m.px, m.qty))
            return;
        qty_t remaining = m.qty;
        match_<Side>(m, remaining);
        if (remaining == 0)
            return;
        if (m.t == tif::ioc || m.t == tif::fok)
            return;
        rest_<Side>(m, remaining);
    }

    template <side Side>
    [[nodiscard]] bool can_fully_fill_(tick_t aggressor_px, qty_t want) const noexcept {
        qty_t total = 0;
        if constexpr (Side == side::bid) {
            for (const auto& [px, fifo] : asks) {
                if (px > aggressor_px)
                    break;
                for (const auto& r : fifo)
                    total += r.remaining;
                if (total >= want)
                    return true;
            }
        } else {
            for (const auto& [px, fifo] : bids) {
                if (px < aggressor_px)
                    break;
                for (const auto& r : fifo)
                    total += r.remaining;
                if (total >= want)
                    return true;
            }
        }
        return total >= want;
    }

    template <side Side>
    void match_(const submit_msg& m, qty_t& remaining) noexcept {
        auto crosses = [&](tick_t opp_px) {
            if constexpr (Side == side::bid)
                return m.px >= opp_px;
            else
                return m.px <= opp_px;
        };
        while (remaining > 0) {
            auto best_opt = (Side == side::bid) ? best_ask() : best_bid();
            if (!best_opt.has_value() || !crosses(*best_opt))
                return;
            const auto best_px = *best_opt;
            auto& fifo =
                (Side == side::bid) ? asks.find(best_px)->second : bids.find(best_px)->second;
            while (remaining > 0 && !fifo.empty()) {
                auto& maker = fifo.front();
                if (m.account_id != 0 && maker.account_id == m.account_id) {
                    if (dispatch_self_cross_<Side>(m, maker, fifo, best_px, remaining)) {
                        if (fifo.empty())
                            drop_empty_level_<Side>(best_px);
                        return;
                    }
                    continue;
                }
                const auto trade_qty = std::min(remaining, maker.remaining);
                ++seq;
                fills.push_back(fill_msg{
                    .maker = maker.id,
                    .taker = m.id,
                    .px = best_px,
                    .qty = trade_qty,
                    .seq = seq,
                });
                trades.push_back(trade_msg{.px = best_px, .qty = trade_qty, .seq = seq});
                maker.remaining -= trade_qty;
                remaining -= trade_qty;
                top_dirty = true;
                if (maker.remaining == 0) {
                    idx.erase(maker.id);
                    fifo.pop_front();
                }
            }
            if (fifo.empty())
                drop_empty_level_<Side>(best_px);
        }
    }

    template <side Side>
    bool dispatch_self_cross_(const submit_msg& m,
                              rec& maker,
                              std::list<rec>& fifo,
                              tick_t best_px,
                              qty_t& remaining) noexcept {
        switch (cfg.self_cross) {
        case self_cross_policy::cancel_newest:
            remaining = 0;
            return true;
        case self_cross_policy::cancel_oldest:
            idx.erase(maker.id);
            fifo.pop_front();
            top_dirty = true;
            return false;
        case self_cross_policy::decrement_trade: {
            const auto trade_qty = std::min(remaining, maker.remaining);
            ++seq;
            self_trades.push_back(self_trade_msg{
                .aggressor = m.id,
                .resting = maker.id,
                .account = m.account_id,
                .px = best_px,
                .qty = trade_qty,
                .seq = seq,
            });
            maker.remaining -= trade_qty;
            remaining -= trade_qty;
            top_dirty = true;
            if (maker.remaining == 0) {
                idx.erase(maker.id);
                fifo.pop_front();
            }
            return false;
        }
        }
        return false;
    }

    template <side Side>
    void drop_empty_level_(tick_t px) noexcept {
        if constexpr (Side == side::bid)
            asks.erase(px);
        else
            bids.erase(px);
    }

    template <side Side>
    void rest_(const submit_msg& m, qty_t remaining) noexcept {
        rec r{.id = m.id,
              .remaining = remaining,
              .px = m.px,
              .s = Side,
              .t = m.t,
              .account_id = m.account_id};
        if constexpr (Side == side::bid) {
            auto& fifo = bids[m.px];
            fifo.push_back(r);
            idx[m.id] = &fifo.back();
        } else {
            auto& fifo = asks[m.px];
            fifo.push_back(r);
            idx[m.id] = &fifo.back();
        }
        top_dirty = true;
    }

    template <side Side>
    void erase_from_(rec* r) noexcept {
        if constexpr (Side == side::bid) {
            auto it = bids.find(r->px);
            if (it == bids.end())
                return;
            for (auto fit = it->second.begin(); fit != it->second.end(); ++fit) {
                if (&*fit == r) {
                    it->second.erase(fit);
                    break;
                }
            }
            if (it->second.empty())
                bids.erase(it);
        } else {
            auto it = asks.find(r->px);
            if (it == asks.end())
                return;
            for (auto fit = it->second.begin(); fit != it->second.end(); ++fit) {
                if (&*fit == r) {
                    it->second.erase(fit);
                    break;
                }
            }
            if (it->second.empty())
                asks.erase(it);
        }
    }

    void publish_top_() noexcept {
        if (!top_dirty)
            return;
        top_dirty = false;
        const auto bb = best_bid();
        const auto ba = best_ask();
        const tick_t bid_px = bb.has_value() ? *bb : tick_t{0};
        const tick_t ask_px = ba.has_value() ? *ba : tick_t{0};
        const qty_t bid_qty = bb.has_value() ? aggregate(side::bid, *bb) : qty_t{0};
        const qty_t ask_qty = ba.has_value() ? aggregate(side::ask, *ba) : qty_t{0};
        if (cfg.top_throttle && have_top && bid_px == last_bid_px && ask_px == last_ask_px &&
            bid_qty == last_bid_qty && ask_qty == last_ask_qty) {
            return;
        }
        ++seq;
        tops.push_back(top_msg{
            .bid_px = bid_px,
            .ask_px = ask_px,
            .bid_qty = bid_qty,
            .ask_qty = ask_qty,
            .seq = seq,
        });
        last_bid_px = bid_px;
        last_ask_px = ask_px;
        last_bid_qty = bid_qty;
        last_ask_qty = ask_qty;
        have_top = true;
    }
};

}  // namespace lob::test

#endif  // LOB_TESTS_REFERENCE_ENGINE_HPP
