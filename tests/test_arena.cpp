#include <lob/arena.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

namespace {

struct alignas(64) cell {
    std::uint64_t a;
    std::uint64_t b;
    std::uint64_t c;
    std::uint64_t d;
    std::byte pad[32];
};

static_assert(sizeof(cell) == 64);

}  // namespace

TEST_CASE("slab_arena starts empty", "[arena]") {
    lob::slab_arena<cell, 16> arena;
    REQUIRE(arena.empty());
    REQUIRE(!arena.full());
    REQUIRE(arena.in_use() == 0);
    REQUIRE(arena.capacity() == 16);
}

TEST_CASE("slab_arena allocate / deallocate round-trip", "[arena]") {
    lob::slab_arena<cell, 8> arena;
    auto* a = arena.allocate();
    auto* b = arena.allocate();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(a != b);
    REQUIRE(arena.in_use() == 2);
    REQUIRE(arena.owns(a));
    REQUIRE(arena.owns(b));
    arena.deallocate(a);
    REQUIRE(arena.in_use() == 1);
    arena.deallocate(b);
    REQUIRE(arena.empty());
}

TEST_CASE("slab_arena saturates at capacity and rejects further allocations", "[arena]") {
    constexpr std::size_t n = 8;
    lob::slab_arena<cell, n> arena;
    std::vector<cell*> live;
    live.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto* p = arena.allocate();
        REQUIRE(p != nullptr);
        live.push_back(p);
    }
    REQUIRE(arena.full());
    REQUIRE(arena.in_use() == n);
    REQUIRE(arena.allocate() == nullptr);
    for (auto* p : live)
        arena.deallocate(p);
    REQUIRE(arena.empty());
}

TEST_CASE("slab_arena allocations are alignof(T)-aligned", "[arena]") {
    lob::slab_arena<cell, 32> arena;
    for (std::size_t i = 0; i < 32; ++i) {
        auto* p = arena.allocate();
        REQUIRE(p != nullptr);
        REQUIRE(reinterpret_cast<std::uintptr_t>(p) % alignof(cell) == 0);
    }
}

TEST_CASE("slab_arena deallocate(nullptr) is a no-op", "[arena]") {
    lob::slab_arena<cell, 4> arena;
    auto* p = arena.allocate();
    REQUIRE(arena.in_use() == 1);
    arena.deallocate(nullptr);
    REQUIRE(arena.in_use() == 1);
    arena.deallocate(p);
    REQUIRE(arena.empty());
}

TEST_CASE("slab_arena LIFO returns the most recently freed slot first", "[arena]") {
    lob::slab_arena<cell, 4> arena;
    auto* a = arena.allocate();
    auto* b = arena.allocate();
    auto* c = arena.allocate();
    arena.deallocate(b);
    arena.deallocate(c);
    auto* d = arena.allocate();
    auto* e = arena.allocate();
    REQUIRE(d == c);
    REQUIRE(e == b);
    arena.deallocate(a);
    arena.deallocate(d);
    arena.deallocate(e);
    REQUIRE(arena.empty());
}

TEST_CASE("slab_arena preserves writes across alloc / dealloc / realloc", "[arena]") {
    lob::slab_arena<cell, 8> arena;
    auto* p = arena.allocate();
    p->a = 0xDEADBEEFCAFEBABEULL;
    p->b = 0x0123456789ABCDEFULL;
    arena.deallocate(p);
    auto* q = arena.allocate();
    REQUIRE(q == p);  // LIFO
    q->a = 0x1111111111111111ULL;
    q->b = 0x2222222222222222ULL;
    REQUIRE(q->a == 0x1111111111111111ULL);
    REQUIRE(q->b == 0x2222222222222222ULL);
}

TEST_CASE("slab_arena differential against std::vector<bool> on random workloads",
          "[arena][property]") {
    constexpr std::size_t cap = 256;
    constexpr std::size_t draws = 2'000;

    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<int> op_dist{0, 1};

    lob::slab_arena<cell, cap> arena;
    std::vector<cell*> live;
    live.reserve(cap);

    for (std::size_t step = 0; step < draws; ++step) {
        const bool wants_alloc = (live.size() < cap) && (live.empty() || op_dist(rng) == 0);
        if (wants_alloc) {
            auto* p = arena.allocate();
            REQUIRE(p != nullptr);
            live.push_back(p);
        } else {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto k = pick(rng);
            arena.deallocate(live[k]);
            live[k] = live.back();
            live.pop_back();
        }
        REQUIRE(arena.in_use() == live.size());
        REQUIRE(arena.empty() == live.empty());
        REQUIRE(arena.full() == (live.size() == cap));
    }
}

TEST_CASE("slab_arena is movable", "[arena]") {
    lob::slab_arena<cell, 8> a;
    auto* p = a.allocate();
    REQUIRE(p != nullptr);
    REQUIRE(a.in_use() == 1);

    lob::slab_arena<cell, 8> b{std::move(a)};
    REQUIRE(b.in_use() == 1);
    REQUIRE(b.owns(p));
    b.deallocate(p);
    REQUIRE(b.empty());
}
