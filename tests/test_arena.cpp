#include <lob/arena.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <rapidcheck/state.h>

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

// The arena is a pool of slots: what it reports about its own occupancy must
// agree with the count of pointers the caller is holding, and a slot handed
// out twice without an intervening free is the defect this catches. RapidCheck
// generates the allocate and deallocate sequences and shrinks a failure.
namespace {

constexpr std::size_t model_cap = 256;

// The model is what the caller holds: how many slots are out. The pointers
// themselves live in the system, because the model is copied as the sequence
// is generated and a pointer means nothing to a copy.
struct pool_model {
    std::size_t live{0};
};

struct arena_sut {
    lob::slab_arena<cell, model_cap> arena{};
    std::vector<cell*> live{};

    void check(const pool_model& m) const {
        RC_ASSERT(arena.in_use() == m.live);
        RC_ASSERT(arena.empty() == (m.live == 0));
        RC_ASSERT(arena.full() == (m.live == model_cap));
        RC_ASSERT(live.size() == m.live);
    }
};

struct take : rc::state::Command<pool_model, arena_sut> {
    void checkPreconditions(const pool_model& m) const override { RC_PRE(m.live < model_cap); }

    void apply(pool_model& m) const override { ++m.live; }

    void run(const pool_model& m, arena_sut& sut) const override {
        auto* p = sut.arena.allocate();
        RC_ASSERT(p != nullptr);
        RC_ASSERT(sut.arena.owns(p));
        // A slot handed out while already held is the failure this sequence
        // exists to find.
        RC_ASSERT(std::find(sut.live.begin(), sut.live.end(), p) == sut.live.end());
        sut.live.push_back(p);
        auto after = m;
        ++after.live;
        sut.check(after);
    }

    void show(std::ostream& os) const override { os << "allocate()"; }
};

struct give_back : rc::state::Command<pool_model, arena_sut> {
    std::size_t k{0};

    explicit give_back(const pool_model& m) {
        RC_PRE(m.live > 0);
        k = *rc::gen::inRange<std::size_t>(0, m.live);
    }

    void checkPreconditions(const pool_model& m) const override { RC_PRE(k < m.live); }

    void apply(pool_model& m) const override { --m.live; }

    void run(const pool_model& m, arena_sut& sut) const override {
        sut.arena.deallocate(sut.live[k]);
        sut.live[k] = sut.live.back();
        sut.live.pop_back();
        auto after = m;
        --after.live;
        sut.check(after);
    }

    void show(std::ostream& os) const override { os << "deallocate(slot=" << k << ")"; }
};

}  // namespace

TEST_CASE("slab_arena hands out each slot to one holder at a time", "[arena][property][model]") {
    rc::prop("occupancy agrees with what the caller holds", [] {
        arena_sut sut;
        rc::state::check(pool_model{}, sut, rc::state::gen::execOneOfWithArgs<take, give_back>());
        RC_CLASSIFY(sut.arena.full(), "filled the arena");
        RC_CLASSIFY(!sut.live.empty(), "left slots out");
    });
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

TEST_CASE("slab_arena generation is odd while a slot is allocated", "[arena][handle]") {
    lob::slab_arena<cell, 4, /*Generations=*/true> arena;
    REQUIRE(arena.generation(0) == 0);

    auto* a = arena.allocate();
    const auto i = arena.index_of(a);
    REQUIRE(arena.slot_at(i) == a);
    REQUIRE(arena.generation(i) == 1);

    arena.deallocate(a);
    REQUIRE(arena.generation(i) == 2);

    // LIFO reuse hands the same slot back under a new generation.
    auto* b = arena.allocate();
    REQUIRE(b == a);
    REQUIRE(arena.generation(i) == 3);
}
