#ifndef LOB_TEST_MODEL_COMMANDS_HPP
#define LOB_TEST_MODEL_COMMANDS_HPP

// Generated command sequences for the engine, shared by every property that
// drives a book.
//
// Claessen and Hughes' QuickCheck (doi:10.1145/351240.351266) generates inputs
// from the domain and shrinks a failure to its shortest form; Hughes'
// treatment of stateful systems (doi:10.1007/978-3-540-74130-5_9) models the
// system as a state machine and generates command sequences valid for the
// model. The model here is the smallest thing a command needs to be generated:
// the next id to hand out, the ids currently live, and the accounts already
// trading.
//
// The properties differ only in what they check after each command, so a
// system supplies two things: push(), which applies the command however that
// property applies it, and check(), which states the property. Everything
// else, the generation and the model transitions, is here and is written once.
//
// No magnitude is fixed here. Enumerations come from magic_enum, so an
// enumerator added to the library is generated the day it is added; prices
// come from the ladder the system under test was instantiated with; and
// quantities and accounts scale with RapidCheck's size parameter.

#include <lob/config.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <vector>

#include <rapidcheck.h>
#include <rapidcheck/state.h>
#include <magic_enum/magic_enum.hpp>

namespace lob::test::cmds {

struct model {
    lob::order_id_t next_id{1};
    std::vector<lob::order_id_t> live;
    // Accounts already trading, so a generated order can join one rather than
    // always opening a new one. A self cross needs two orders of one account
    // to meet, and a generator that never reuses an account never reaches it.
    std::vector<lob::account_id_t> accounts;
};

template <class Enum>
[[nodiscard]] inline Enum gen_enum() {
    return *rc::gen::elementOf(magic_enum::enum_values<Enum>());
}

// Quantity is positive and at most the engine's configured cap: that is the
// contract the gateway validator enforces before an order reaches the engine
// (see engine_config::max_order_qty), so a generator that exceeded it would be
// testing the engine outside the range it is built for. Within the cap, how
// large is the size parameter's business, which is what grows a property from
// small cases to large ones as a run progresses.
[[nodiscard]] inline lob::qty_t gen_qty() {
    constexpr lob::qty_t cap = lob::engine_config{}.max_order_qty;
    return std::min<lob::qty_t>(*rc::gen::positive<lob::qty_t>(), cap);
}

[[nodiscard]] inline lob::tick_t gen_price(std::size_t ticks) {
    return *rc::gen::inRange<lob::tick_t>(0, static_cast<lob::tick_t>(ticks));
}

// Either an account already trading or a new one, with no preference between
// them.
[[nodiscard]] inline lob::account_id_t gen_account(const model& m) {
    if (m.accounts.empty())
        return *rc::gen::arbitrary<lob::account_id_t>();
    return *rc::gen::oneOf(rc::gen::elementOf(m.accounts), rc::gen::arbitrary<lob::account_id_t>());
}

inline void note_account(model& m, lob::account_id_t account) {
    if (std::find(m.accounts.begin(), m.accounts.end(), account) == m.accounts.end())
        m.accounts.push_back(account);
}

// A submit drawn from the whole domain: either side, any time-in-force, any
// price on the ladder, an account new or already trading.
template <class Sys>
struct submit : rc::state::Command<model, Sys> {
    lob::side s{};
    lob::tick_t px{0};
    lob::qty_t qty{1};
    lob::tif t{};
    lob::account_id_t account{0};

    explicit submit(const model& m)
        : s{gen_enum<lob::side>()},
          px{gen_price(Sys::ticks)},
          qty{gen_qty()},
          t{gen_enum<lob::tif>()},
          account{gen_account(m)} {}

    void apply(model& m) const override {
        m.live.push_back(m.next_id);
        ++m.next_id;
        note_account(m, account);
    }

    void run(const model& m, Sys& sut) const override {
        const auto c = lob::command::make_submit(lob::submit_msg{
            .id = m.next_id,
            .px = px,
            .qty = qty,
            .s = s,
            .t = t,
            ._pad = 0,
            .account_id = account,
        });
        sut.push(c);
        sut.check(c);
    }

    void show(std::ostream& os) const override {
        os << "submit(px=" << px << ", qty=" << qty << ", side=" << magic_enum::enum_name(s)
           << ", tif=" << magic_enum::enum_name(t) << ", account=" << account << ")";
    }
};

// A submit that always rests if it can: good-till-cancelled, no account, so
// nothing is cancelled by a self-cross policy and nothing expires. What a
// property about conservation or about capacity wants.
template <class Sys>
struct rest : rc::state::Command<model, Sys> {
    lob::side s{};
    lob::tick_t px{0};
    lob::qty_t qty{1};

    explicit rest(const model&) : s{side_()}, px{gen_price(Sys::ticks)}, qty{gen_qty()} {}

    void apply(model& m) const override {
        m.live.push_back(m.next_id);
        ++m.next_id;
    }

    void run(const model& m, Sys& sut) const override {
        const auto c = lob::command::make_submit(lob::submit_msg{
            .id = m.next_id,
            .px = px,
            .qty = qty,
            .s = s,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0,
        });
        sut.push(c);
        sut.check(c);
    }

    void show(std::ostream& os) const override {
        os << "rest(px=" << px << ", qty=" << qty << ", side=" << magic_enum::enum_name(s) << ")";
    }

   private:
    // A system that fixes a side gets that side, so its book cannot match
    // itself; one that does not gets both.
    [[nodiscard]] static lob::side side_() {
        if constexpr (requires { Sys::resting_side; })
            return Sys::resting_side;
        else
            return gen_enum<lob::side>();
    }
};

template <class Sys>
struct cancel : rc::state::Command<model, Sys> {
    std::size_t k{0};

    explicit cancel(const model& m) {
        RC_PRE(!m.live.empty());
        k = *rc::gen::inRange<std::size_t>(0, m.live.size());
    }

    void checkPreconditions(const model& m) const override { RC_PRE(k < m.live.size()); }

    void apply(model& m) const override {
        m.live[k] = m.live.back();
        m.live.pop_back();
    }

    void run(const model& m, Sys& sut) const override {
        const auto c = lob::command::make_cancel(lob::cancel_msg{.id = m.live[k]});
        sut.push(c);
        sut.check(c);
    }

    void show(std::ostream& os) const override { os << "cancel(slot=" << k << ")"; }
};

template <class Sys>
struct modify : rc::state::Command<model, Sys> {
    std::size_t k{0};
    lob::tick_t px{0};
    lob::qty_t qty{1};
    bool rename{false};

    explicit modify(const model& m) {
        RC_PRE(!m.live.empty());
        k = *rc::gen::inRange<std::size_t>(0, m.live.size());
        px = gen_price(Sys::ticks);
        qty = gen_qty();
        // A cancel-replace renames the order, so the old id stops naming
        // anything and later commands must use the new one.
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
        const auto c = lob::command::make_modify(lob::modify_msg{
            .id = m.live[k],
            .new_px = px,
            .new_qty = qty,
            .new_id = rename ? m.next_id : lob::order_id_t{0},
        });
        sut.push(c);
        sut.check(c);
    }

    void show(std::ostream& os) const override {
        os << "modify(slot=" << k << ", px=" << px << ", qty=" << qty
           << ", rename=" << (rename ? "yes" : "no") << ")";
    }
};

// Runs one generated sequence against a system built in place, then lets the
// system state whatever it can only check at the end.
template <class Sys, template <class> class... Cmds, class... Args>
void check_sequence(Args&&... args) {
    Sys sut(std::forward<Args>(args)...);
    rc::state::check(model{}, sut, rc::state::gen::execOneOfWithArgs<Cmds<Sys>...>());
    sut.finish();
}

}  // namespace lob::test::cmds

#endif  // LOB_TEST_MODEL_COMMANDS_HPP
