#include <lob/id_index.hpp>
#include <lob/order.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

#include <array>
#include <cstddef>
#include <random>
#include <unordered_map>

TEST_CASE("id_index empty state", "[id_index]") {
    lob::id_index idx;
    REQUIRE(idx.empty());
    REQUIRE(idx.size() == 0);
    REQUIRE(idx.lookup(42) == nullptr);
}

TEST_CASE("id_index insert / lookup / erase round-trip", "[id_index]") {
    lob::id_index idx{16};
    std::array<lob::order, 4> orders{};
    for (std::size_t i = 0; i < orders.size(); ++i) orders[i].id = 100 + i;

    for (auto& o : orders) idx.insert(o.id, &o);
    REQUIRE(idx.size() == orders.size());

    REQUIRE(idx.lookup(100) == &orders[0]);
    REQUIRE(idx.lookup(103) == &orders[3]);
    REQUIRE(idx.lookup(999) == nullptr);

    idx.erase(101);
    REQUIRE(idx.size() == 3);
    REQUIRE(idx.lookup(101) == nullptr);

    idx.clear();
    REQUIRE(idx.empty());
}

TEST_CASE("id_index differential against std::unordered_map on random workloads", "[id_index][property]") {
    constexpr std::size_t cap   = 256;
    constexpr std::size_t draws = 2'000;

    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int>             op_dist{0, 2};
    std::uniform_int_distribution<lob::order_id_t> id_dist{0, cap - 1};

    std::array<lob::order, cap> pool{};
    for (std::size_t i = 0; i < pool.size(); ++i) pool[i].id = i;

    lob::id_index                                  idx;
    std::unordered_map<lob::order_id_t, lob::order*> oracle;

    for (std::size_t step = 0; step < draws; ++step) {
        const auto id = id_dist(rng);
        switch (op_dist(rng)) {
            case 0: {
                idx.insert(id, &pool[id]);
                oracle[id] = &pool[id];
                break;
            }
            case 1: {
                idx.erase(id);
                oracle.erase(id);
                break;
            }
            case 2: {
                const auto* got      = idx.lookup(id);
                const auto  it       = oracle.find(id);
                const auto* expected = (it == oracle.end()) ? nullptr : it->second;
                REQUIRE(got == expected);
                break;
            }
            default:
                break;
        }
        REQUIRE(idx.size() == oracle.size());
    }
}
