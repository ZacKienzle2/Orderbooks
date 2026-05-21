#include <lob/bitmap.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

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

TEST_CASE("hier_bitmap differential against std::set on random workloads", "[bitmap][property]") {
    constexpr std::size_t cap = 4096;
    constexpr std::size_t draws = 4'000;

    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL, 0x1234567890ABCDEFULL);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<std::size_t> bit_dist{0, cap - 1};
    std::uniform_int_distribution<int> op_dist{0, 2};

    hier_bitmap<cap> bm;
    std::set<std::size_t> oracle;

    for (std::size_t step = 0; step < draws; ++step) {
        const auto bit = bit_dist(rng);
        switch (op_dist(rng)) {
        case 0:
            bm.set(bit);
            oracle.insert(bit);
            break;
        case 1:
            bm.clear(bit);
            oracle.erase(bit);
            break;
        case 2:
            REQUIRE(bm.test(bit) == (oracle.count(bit) > 0));
            break;
        default:
            break;
        }
        REQUIRE(bm.empty() == oracle.empty());
        if (!oracle.empty()) {
            REQUIRE(bm.lowest_set() == *oracle.begin());
            REQUIRE(bm.highest_set() == *oracle.rbegin());
        }
    }
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
        0,
        63,
        64,
        4096,
        262'143,
        cap - 1,
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

TEST_CASE("hier_bitmap next / prev differential against std::set", "[bitmap][property]") {
    constexpr std::size_t cap = 4096;
    constexpr std::size_t draws = 1'000;

    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<std::size_t> bit_dist{0, cap - 1};

    hier_bitmap<cap> bm;
    std::set<std::size_t> oracle;
    for (std::size_t i = 0; i < 200; ++i) {
        const auto b = bit_dist(rng);
        bm.set(b);
        oracle.insert(b);
    }

    for (std::size_t step = 0; step < draws; ++step) {
        const auto q = bit_dist(rng);

        const auto next_actual = bm.next_set_at_or_after(q);
        const auto it_next = oracle.lower_bound(q);
        const auto next_expected =
            (it_next == oracle.end()) ? std::nullopt : std::optional<std::size_t>{*it_next};
        REQUIRE(next_actual == next_expected);

        const auto prev_actual = bm.prev_set_at_or_before(q);
        const auto it_prev = oracle.upper_bound(q);
        const auto prev_expected = (it_prev == oracle.begin())
                                       ? std::nullopt
                                       : std::optional<std::size_t>{*std::prev(it_prev)};
        REQUIRE(prev_actual == prev_expected);
    }
}
