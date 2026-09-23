#include <lob/bitmap.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ostream>
#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <rapidcheck/state.h>

using lob::hier_bitmap;

TEST_CASE("hier_bitmap default-constructs empty", "[bitmap]") {
    hier_bitmap<128> bm;
    REQUIRE(bm.empty());
    REQUIRE(!bm.lowest_set().has_value());
    REQUIRE(!bm.highest_set().has_value());
}

TEST_CASE("hier_bitmap single bit set / clear cycle", "[bitmap]") {
    hier_bitmap<256> bm;
    bm.set(42);
    REQUIRE(!bm.empty());
    REQUIRE(bm.test(42));
    REQUIRE(bm.lowest_set() == 42);
    REQUIRE(bm.highest_set() == 42);
    bm.clear(42);
    REQUIRE(bm.empty());
    REQUIRE(!bm.test(42));
}

TEST_CASE("hier_bitmap set is idempotent", "[bitmap]") {
    hier_bitmap<64> bm;
    bm.set(7);
    bm.set(7);
    bm.set(7);
    REQUIRE(bm.lowest_set() == 7);
    bm.clear(7);
    REQUIRE(bm.empty());
}

TEST_CASE("hier_bitmap clear of an unset bit is a no-op", "[bitmap]") {
    hier_bitmap<64> bm;
    bm.set(3);
    bm.clear(9);
    REQUIRE(bm.lowest_set() == 3);
    REQUIRE(bm.highest_set() == 3);
}

TEST_CASE("hier_bitmap lowest_set + highest_set with multiple bits", "[bitmap]") {
    hier_bitmap<4096> bm;
    for (auto b : {17U, 65U, 128U, 4095U, 1024U})
        bm.set(b);
    REQUIRE(bm.lowest_set() == 17);
    REQUIRE(bm.highest_set() == 4095);
    bm.clear(4095);
    REQUIRE(bm.highest_set() == 1024);
    bm.clear(17);
    REQUIRE(bm.lowest_set() == 65);
}

TEST_CASE("hier_bitmap saturate-and-drain across word boundaries", "[bitmap]") {
    constexpr std::size_t cap = 256;
    hier_bitmap<cap> bm;
    for (std::size_t i = 0; i < cap; ++i)
        bm.set(i);
    REQUIRE(bm.lowest_set() == 0);
    REQUIRE(bm.highest_set() == cap - 1);
    for (std::size_t i = 0; i < cap; ++i)
        bm.clear(i);
    REQUIRE(bm.empty());
}

TEST_CASE("hier_bitmap honours bit-63 / bit-64 boundary", "[bitmap]") {
    hier_bitmap<128> bm;
    bm.set(63);
    bm.set(64);
    REQUIRE(bm.lowest_set() == 63);
    REQUIRE(bm.highest_set() == 64);
    bm.clear(63);
    REQUIRE(bm.lowest_set() == 64);
    bm.clear(64);
    REQUIRE(bm.empty());
}

TEST_CASE("hier_bitmap three-tier capacity round-trip", "[bitmap]") {
    // 4096 ticks -> L0_W = 64, L1_W = 1, L2_W = 0, L3_W = 0.
    constexpr std::size_t cap = 4096;
    hier_bitmap<cap> bm;
    bm.set(cap - 1);
    bm.set(0);
    bm.set(2048);
    REQUIRE(bm.lowest_set() == 0);
    REQUIRE(bm.highest_set() == cap - 1);
    bm.clear(0);
    REQUIRE(bm.lowest_set() == 2048);
    bm.clear(2048);
    bm.clear(cap - 1);
    REQUIRE(bm.empty());
}

TEST_CASE("hier_bitmap four-tier capacity round-trip", "[bitmap]") {
    // 1 << 20 ticks exercises all four tiers (L0_W=16384, L1_W=256, L2_W=4, L3_W=1).
    constexpr std::size_t cap = 1U << 20;
    hier_bitmap<cap> bm;
    bm.set(0);
    bm.set(cap - 1);
    bm.set(cap / 2);
    REQUIRE(bm.lowest_set() == 0);
    REQUIRE(bm.highest_set() == cap - 1);
    bm.clear(0);
    REQUIRE(bm.lowest_set() == cap / 2);
    bm.clear(cap / 2);
    REQUIRE(bm.lowest_set() == cap - 1);
    bm.clear(cap - 1);
    REQUIRE(bm.empty());
}

// The bitmap is a set of bit positions, so std::set is its model and
// RapidCheck generates the operation sequences. A failure shrinks to the
// shortest sequence of sets and clears that breaks an answer, which a fixed
// stream of four thousand draws could not report.
namespace {

// The ladder where both of the bitmap's levels are exactly full, read off the
// machine word it indexes with.
constexpr std::size_t word_bits = std::numeric_limits<std::uint64_t>::digits;
constexpr std::size_t model_cap = word_bits * word_bits;

using bit_model = std::set<std::size_t>;

struct bitmap_sut {
    hier_bitmap<model_cap> bm;
};

// Everything the bitmap reports, against everything the model says, including
// the queries at a generated point rather than only at the extremes.
void check_against_model(const bitmap_sut& sut, const bit_model& m) {
    const auto q = *rc::gen::inRange<std::size_t>(0, model_cap);
    RC_ASSERT(sut.bm.empty() == m.empty());
    RC_ASSERT(sut.bm.test(q) == m.contains(q));
    if (!m.empty()) {
        RC_ASSERT(sut.bm.lowest_set() == *m.begin());
        RC_ASSERT(sut.bm.highest_set() == *m.rbegin());
    }

    const auto it_next = m.lower_bound(q);
    const auto next_expected =
        (it_next == m.end()) ? std::nullopt : std::optional<std::size_t>{*it_next};
    RC_ASSERT(sut.bm.next_set_at_or_after(q) == next_expected);

    const auto it_prev = m.upper_bound(q);
    const auto prev_expected =
        (it_prev == m.begin()) ? std::nullopt : std::optional<std::size_t>{*std::prev(it_prev)};
    RC_ASSERT(sut.bm.prev_set_at_or_before(q) == prev_expected);
}

struct set_bit : rc::state::Command<bit_model, bitmap_sut> {
    std::size_t bit{0};

    explicit set_bit(const bit_model&) : bit{*rc::gen::inRange<std::size_t>(0, model_cap)} {}

    void apply(bit_model& m) const override { m.insert(bit); }

    void run(const bit_model& m, bitmap_sut& sut) const override {
        sut.bm.set(bit);
        auto after = m;
        after.insert(bit);
        check_against_model(sut, after);
    }

    void show(std::ostream& os) const override { os << "set(" << bit << ")"; }
};

// Clearing a bit that is set exercises the tier unwind; clearing one that is
// not must be harmless. Both are drawn, with no preference between them.
struct clear_bit : rc::state::Command<bit_model, bitmap_sut> {
    std::size_t bit{0};

    explicit clear_bit(const bit_model& m)
        : bit{m.empty() ? *rc::gen::inRange<std::size_t>(0, model_cap)
                        : *rc::gen::oneOf(rc::gen::elementOf(m),
                                          rc::gen::inRange<std::size_t>(0, model_cap))} {}

    void apply(bit_model& m) const override { m.erase(bit); }

    void run(const bit_model& m, bitmap_sut& sut) const override {
        sut.bm.clear(bit);
        auto after = m;
        after.erase(bit);
        check_against_model(sut, after);
    }

    void show(std::ostream& os) const override { os << "clear(" << bit << ")"; }
};

}  // namespace

TEST_CASE("hier_bitmap answers as the set it represents", "[bitmap][property][model]") {
    rc::prop("every query agrees with std::set after every set and clear", [] {
        bitmap_sut sut;
        rc::state::check(bit_model{}, sut, rc::state::gen::execOneOfWithArgs<set_bit, clear_bit>());
        RC_CLASSIFY(!sut.bm.empty(), "left bits set");
    });
}

TEST_CASE("hier_bitmap clear_all wipes every tier", "[bitmap]") {
    hier_bitmap<4096> bm;
    for (std::size_t b = 0; b < 4096; b += 7)
        bm.set(b);
    REQUIRE(!bm.empty());
    bm.clear_all();
    REQUIRE(bm.empty());
    REQUIRE(!bm.lowest_set().has_value());
    REQUIRE(!bm.highest_set().has_value());
}

TEST_CASE("hier_bitmap next_set_at_or_after edge cases", "[bitmap]") {
    hier_bitmap<4096> bm;
    REQUIRE(!bm.next_set_at_or_after(0).has_value());
    REQUIRE(!bm.next_set_at_or_after(4095).has_value());
    REQUIRE(!bm.next_set_at_or_after(4096).has_value());

    bm.set(0);
    REQUIRE(bm.next_set_at_or_after(0) == 0);
    REQUIRE(!bm.next_set_at_or_after(1).has_value());

    bm.clear(0);
    bm.set(4095);
    REQUIRE(bm.next_set_at_or_after(0) == 4095);
    REQUIRE(bm.next_set_at_or_after(4095) == 4095);
}

TEST_CASE("hier_bitmap prev_set_at_or_before edge cases", "[bitmap]") {
    hier_bitmap<4096> bm;
    REQUIRE(!bm.prev_set_at_or_before(4095).has_value());
    REQUIRE(!bm.prev_set_at_or_before(0).has_value());

    bm.set(4095);
    REQUIRE(bm.prev_set_at_or_before(4095) == 4095);
    REQUIRE(!bm.prev_set_at_or_before(4094).has_value());

    bm.clear(4095);
    bm.set(0);
    REQUIRE(bm.prev_set_at_or_before(4095) == 0);
    REQUIRE(bm.prev_set_at_or_before(0) == 0);
}

TEST_CASE("hier_bitmap next / prev walk monotonically across a four-tier configuration",
          "[bitmap]") {
    constexpr std::size_t cap = 1U << 20;
    hier_bitmap<cap> bm;

    // Set bits across L0 word boundaries and tier transitions.
    const std::array<std::size_t, 6> bits{
        0, 63, 64, 4096, 262'143, cap - 1,
    };
    for (auto b : bits)
        bm.set(b);

    // Walk forward via next_set_at_or_after from 0.
    std::size_t cursor = 0;
    std::vector<std::size_t> walked_forward;
    while (true) {
        auto next = bm.next_set_at_or_after(cursor);
        if (!next.has_value())
            break;
        walked_forward.push_back(*next);
        if (*next == cap - 1)
            break;
        cursor = *next + 1;
    }
    REQUIRE(walked_forward.size() == bits.size());
    for (std::size_t i = 0; i < bits.size(); ++i) {
        REQUIRE(walked_forward[i] == bits[i]);
    }

    // Walk backward via prev_set_at_or_before from cap-1.
    cursor = cap - 1;
    std::vector<std::size_t> walked_backward;
    while (true) {
        auto prev = bm.prev_set_at_or_before(cursor);
        if (!prev.has_value())
            break;
        walked_backward.push_back(*prev);
        if (*prev == 0)
            break;
        cursor = *prev - 1;
    }
    REQUIRE(walked_backward.size() == bits.size());
    for (std::size_t i = 0; i < bits.size(); ++i) {
        REQUIRE(walked_backward[i] == bits[bits.size() - 1 - i]);
    }
}
