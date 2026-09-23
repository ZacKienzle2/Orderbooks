#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/shard_worker.hpp>
#include <lob/snapshot.hpp>
#include <lob/types.hpp>

#include "model_commands.hpp"
#include "recording_publisher.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <boost/pfr/ops.hpp>
#include <magic_enum/magic_enum.hpp>

namespace {

namespace cmds = lob::test::cmds;

// The ladder where both of the hierarchical bitmap's levels are exactly full,
// read off the machine word it indexes with.
constexpr std::size_t word_bits = std::numeric_limits<std::uint64_t>::digits;
constexpr std::size_t ticks = word_bits * word_bits;
constexpr std::size_t max_ord = ticks;

using pub_t = lob::test::recording_publisher;
using eng_t = lob::engine<pub_t, ticks, max_ord>;

lob::submit_msg sub_with_account(lob::order_id_t id,
                                 lob::tick_t px,
                                 lob::qty_t qty,
                                 lob::side s,
                                 lob::account_id_t acct,
                                 lob::tif t = lob::tif::gtc) {
    return {.id = id, .px = px, .qty = qty, .s = s, .t = t, ._pad = 0, .account_id = acct};
}

// Enough resting orders that a snapshot has records to write. The examples
// below are about the byte stream, not about the book, so what rests only has
// to be non-empty.
void rest_a_few(eng_t& eng) {
    for (lob::order_id_t id = 1; id <= 8; ++id) {
        eng.on_submit(sub_with_account(id, static_cast<lob::tick_t>(id), 5,
                                       (id % 2 == 0) ? lob::side::bid : lob::side::ask, 1));
    }
}

}  // namespace

// A snapshot is a warm start: restoring one must give back the book that was
// taken, and the restored engine must then behave as the original would have.
// Stating it over generated command sequences covers the books a fixed seeded
// stream never builds, and shrinks a failure to the shortest book that breaks.
namespace {

struct snapshot_system {
    static constexpr std::size_t ticks = ::ticks;

    pub_t pub{};
    eng_t eng{pub, lob::engine_config{}};

    void push(const lob::command& c) { lob::apply_command(eng, c); }

    void check(const lob::command&) const noexcept {}

    // Snapshot, restore into a fresh engine, and require the two to be
    // indistinguishable: equal books, equal sequence, and equal events from
    // the same follow-up orders.
    void finish() {
        lob::vector_snapshot_buffer buf;
        eng.snapshot(buf);
        buf.rewind();

        pub_t restored_pub;
        eng_t restored{restored_pub, lob::engine_config{}};
        RC_ASSERT(restored.restore(buf));

        RC_ASSERT(eng.book_view().bids().best() == restored.book_view().bids().best());
        RC_ASSERT(eng.book_view().asks().best() == restored.book_view().asks().best());
        for (lob::tick_t px = 0; px < ticks; ++px) {
            RC_ASSERT(eng.book_view().bids().aggregate_at(px) ==
                      restored.book_view().bids().aggregate_at(px));
            RC_ASSERT(eng.book_view().asks().aggregate_at(px) ==
                      restored.book_view().asks().aggregate_at(px));
        }
        RC_ASSERT(eng.last_seq() == restored.last_seq());

        // From here the two engines are driven identically, so every event
        // either publishes must match. This is what a warm start is for.
        pub.clear();
        restored_pub.clear();
        const auto follow_ups = *rc::gen::inRange<std::size_t>(0, ticks);
        lob::order_id_t next = eng.last_seq() + ticks;
        for (std::size_t i = 0; i < follow_ups; ++i) {
            const lob::submit_msg m{
                .id = next++,
                .px = cmds::gen_price(ticks),
                .qty = cmds::gen_qty(),
                .s = cmds::gen_enum<lob::side>(),
                .t = cmds::gen_enum<lob::tif>(),
                ._pad = 0,
                .account_id = 0,
            };
            eng.on_submit(m);
            restored.on_submit(m);
        }
        RC_ASSERT(pub.fills.size() == restored_pub.fills.size());
        for (std::size_t i = 0; i < pub.fills.size(); ++i)
            RC_ASSERT(boost::pfr::eq(pub.fills[i], restored_pub.fills[i]));
        RC_ASSERT(pub.tops.size() == restored_pub.tops.size());
        for (std::size_t i = 0; i < pub.tops.size(); ++i)
            RC_ASSERT(boost::pfr::eq(pub.tops[i], restored_pub.tops[i]));
        RC_CLASSIFY(!pub.fills.empty(), "matched after the warm start");
    }
};

}  // namespace

TEST_CASE("engine snapshot restores a book that carries on identically",
          "[engine][snapshot][model]") {
    rc::prop("restore reproduces the book and the events that follow it", [] {
        cmds::check_sequence<snapshot_system, cmds::rest, cmds::cancel, cmds::modify>();
    });
}

TEST_CASE("engine restore rejects header from an incompatible engine shape", "[engine][snapshot]") {
    using small_eng_t = lob::engine<pub_t, 64, 16>;

    pub_t small_pub;
    small_eng_t small_engine{small_pub, lob::engine_config{}};
    small_engine.on_submit(sub_with_account(1, 10, 5, lob::side::bid, 1));

    lob::vector_snapshot_buffer buf;
    small_engine.snapshot(buf);
    buf.rewind();

    pub_t pub_big;
    eng_t engine_big{pub_big, lob::engine_config{}};
    REQUIRE_FALSE(engine_big.restore(buf));
    // After a rejected restore the engine is left in a freshly cleared state.
    REQUIRE_FALSE(engine_big.book_view().bids().best().has_value());
    REQUIRE_FALSE(engine_big.book_view().asks().best().has_value());
}

namespace {

// Fixed-capacity sink that satisfies snapshot_sink. Throws std::bad_alloc
// when the cumulative write exceeds the configured budget; the engine
// state remains valid through and after the throw.
class fixed_capacity_sink {
   public:
    explicit fixed_capacity_sink(std::size_t cap) : cap_(cap) {}

    void write(std::span<const std::byte> bytes) {
        if (used_ + bytes.size() > cap_)
            throw std::bad_alloc{};
        used_ += bytes.size();
    }

    [[nodiscard]] std::size_t used() const noexcept { return used_; }

   private:
    std::size_t cap_;
    std::size_t used_{0};
};

}  // namespace

TEST_CASE("engine snapshot propagates sink throw with engine state intact", "[engine][snapshot]") {
    pub_t pub;
    eng_t eng{pub, lob::engine_config{}};
    rest_a_few(eng);
    const auto best_bid = eng.book_view().bids().best();
    const auto best_ask = eng.book_view().asks().best();
    const auto seq_before = eng.last_seq();

    // Header is 80 bytes; fix the budget at 64 so write throws before the
    // first record lands. The throw must escape engine::snapshot cleanly
    // and the engine must be byte-equal to its pre-call state.
    fixed_capacity_sink sink{64};
    REQUIRE_THROWS_AS(eng.snapshot(sink), std::bad_alloc);

    REQUIRE(eng.book_view().bids().best() == best_bid);
    REQUIRE(eng.book_view().asks().best() == best_ask);
    REQUIRE(eng.last_seq() == seq_before);
}

TEST_CASE("engine restore rejects a truncated snapshot", "[engine][snapshot]") {
    pub_t pub_a;
    eng_t engine_a{pub_a, lob::engine_config{}};
    rest_a_few(engine_a);

    lob::vector_snapshot_buffer buf;
    engine_a.snapshot(buf);
    // Drop the trailing record half-way through.
    std::vector<std::byte> bytes;
    bytes.resize(buf.size() / 2);
    {
        buf.rewind();
        std::span<std::byte> first_half{bytes.data(), bytes.size()};
        REQUIRE(buf.read(first_half));
    }
    lob::vector_snapshot_buffer truncated;
    truncated.write(std::span<const std::byte>{bytes.data(), bytes.size()});
    truncated.rewind();

    pub_t pub_b;
    eng_t engine_b{pub_b, lob::engine_config{}};
    REQUIRE_FALSE(engine_b.restore(truncated));
}

namespace {

template <class T>
void put_bytes(lob::vector_snapshot_buffer& buf, const T& v) {
    const void* p = &v;
    buf.write(std::span<const std::byte>{static_cast<const std::byte*>(p), sizeof v});
}

lob::snapshot_header valid_header(std::uint64_t num_orders) {
    lob::snapshot_header hdr{};
    hdr.ticks = ticks;
    hdr.max_orders = max_ord;
    hdr.num_orders = num_orders;
    return hdr;
}

lob::snapshot_order_record valid_record() {
    return {.id = 1, .remaining = 5, .px = 10, .s = 0, .t = 0, ._pad0 = 0, .account_id = 0};
}

void require_cleared(const eng_t& eng) {
    REQUIRE(!eng.book_view().bids().best().has_value());
    REQUIRE(!eng.book_view().asks().best().has_value());
}

}  // namespace

// Snapshot bytes come from an external source. A value the engine could
// never have emitted must be rejected before it reaches an enum cast or an
// unchecked ladder index, and the rejection must leave the engine cleared.
TEST_CASE("engine restore rejects out-of-range header and record fields", "[engine][snapshot]") {
    pub_t pub;
    eng_t eng{pub, lob::engine_config{}};

    SECTION("valid handcrafted blob restores") {
        lob::vector_snapshot_buffer buf;
        put_bytes(buf, valid_header(1));
        put_bytes(buf, valid_record());
        REQUIRE(eng.restore(buf));
        REQUIRE(eng.book_view().bids().aggregate_at(10) == 5);
    }

    SECTION("self_cross byte beyond the enumerators") {
        auto hdr = valid_header(0);
        hdr.self_cross =
            static_cast<std::uint8_t>(magic_enum::enum_count<lob::self_cross_policy>());
        lob::vector_snapshot_buffer buf;
        put_bytes(buf, hdr);
        REQUIRE_FALSE(eng.restore(buf));
        require_cleared(eng);
    }

    SECTION("record px off the ladder") {
        auto rec = valid_record();
        rec.px = ticks;
        lob::vector_snapshot_buffer buf;
        put_bytes(buf, valid_header(1));
        put_bytes(buf, rec);
        REQUIRE_FALSE(eng.restore(buf));
        require_cleared(eng);
    }

    SECTION("record side byte beyond the enumerators") {
        auto rec = valid_record();
        rec.s = static_cast<std::uint8_t>(magic_enum::enum_count<lob::side>());
        lob::vector_snapshot_buffer buf;
        put_bytes(buf, valid_header(1));
        put_bytes(buf, rec);
        REQUIRE_FALSE(eng.restore(buf));
        require_cleared(eng);
    }

    SECTION("record tif byte beyond the enumerators") {
        auto rec = valid_record();
        rec.t = static_cast<std::uint8_t>(magic_enum::enum_count<lob::tif>());
        lob::vector_snapshot_buffer buf;
        put_bytes(buf, valid_header(1));
        put_bytes(buf, rec);
        REQUIRE_FALSE(eng.restore(buf));
        require_cleared(eng);
    }

    SECTION("record with zero remaining quantity") {
        auto rec = valid_record();
        rec.remaining = 0;
        lob::vector_snapshot_buffer buf;
        put_bytes(buf, valid_header(1));
        put_bytes(buf, rec);
        REQUIRE_FALSE(eng.restore(buf));
        require_cleared(eng);
    }

    SECTION("record with a reserved order id") {
        for (const auto bad_id : {std::uint64_t{0}, ~std::uint64_t{0}}) {
            auto rec = valid_record();
            rec.id = bad_id;
            lob::vector_snapshot_buffer buf;
            put_bytes(buf, valid_header(1));
            put_bytes(buf, rec);
            REQUIRE_FALSE(eng.restore(buf));
            require_cleared(eng);
        }
    }

    SECTION("record with remaining above max_order_qty") {
        auto rec = valid_record();
        rec.remaining = eng.config().max_order_qty + 1;
        lob::vector_snapshot_buffer buf;
        put_bytes(buf, valid_header(1));
        put_bytes(buf, rec);
        REQUIRE_FALSE(eng.restore(buf));
        require_cleared(eng);
    }
}
