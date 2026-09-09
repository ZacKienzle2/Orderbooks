#include <lob/spsc_ring.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

using lob::spsc_ring;

TEST_CASE("spsc_ring default state is empty, not full", "[spsc]") {
    spsc_ring<std::uint64_t, 8> ring;
    REQUIRE(ring.empty());
    REQUIRE_FALSE(ring.full());
    REQUIRE(ring.size() == 0);
    REQUIRE(spsc_ring<std::uint64_t, 8>::capacity() == 8);
}

TEST_CASE("spsc_ring try_pop on empty returns false", "[spsc]") {
    spsc_ring<std::uint64_t, 4> ring;
    std::uint64_t out{0xDEAD};
    REQUIRE_FALSE(ring.try_pop(out));
    REQUIRE(out == 0xDEAD);
}

TEST_CASE("spsc_ring fill and drain in single thread", "[spsc]") {
    constexpr std::size_t cap = 16;
    spsc_ring<std::uint64_t, cap> ring;
    for (std::uint64_t i = 0; i < cap; ++i)
        REQUIRE(ring.try_push(i));
    REQUIRE(ring.full());
    REQUIRE(ring.size() == cap);

    REQUIRE_FALSE(ring.try_push(99));

    for (std::uint64_t i = 0; i < cap; ++i) {
        std::uint64_t out{};
        REQUIRE(ring.try_pop(out));
        REQUIRE(out == i);
    }
    REQUIRE(ring.empty());
}

TEST_CASE("spsc_ring honours FIFO across multiple wrap-arounds", "[spsc]") {
    constexpr std::size_t cap = 8;
    spsc_ring<std::uint64_t, cap> ring;
    std::uint64_t next_push = 0;
    std::uint64_t next_pop = 0;

    for (int cycle = 0; cycle < 100; ++cycle) {
        while (ring.try_push(next_push))
            ++next_push;
        std::uint64_t out{};
        while (ring.try_pop(out)) {
            REQUIRE(out == next_pop);
            ++next_pop;
        }
    }
    REQUIRE(next_push == next_pop);
}

TEST_CASE("spsc_ring producer / consumer thread pair preserves order and count",
          "[spsc][threading]") {
    constexpr std::size_t cap = 1024;
    constexpr std::size_t items = 250'000;
    spsc_ring<std::uint64_t, cap> ring;
    std::atomic<bool> stop{false};

    std::vector<std::uint64_t> consumed;
    consumed.reserve(items);

    std::thread producer{[&] {
        for (std::uint64_t i = 0; i < items; ++i) {
            while (!ring.try_push(i)) {
                std::this_thread::yield();
            }
        }
        stop.store(true, std::memory_order_release);
    }};

    std::thread consumer{[&] {
        std::uint64_t out{};
        while (true) {
            if (ring.try_pop(out)) {
                consumed.push_back(out);
            } else if (stop.load(std::memory_order_acquire) && ring.empty()) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
    }};

    producer.join();
    consumer.join();

    REQUIRE(consumed.size() == items);
    for (std::size_t i = 0; i < items; ++i) {
        REQUIRE(consumed[i] == i);
    }
}

TEST_CASE("spsc_ring with non-trivial-sized POD preserves bytes", "[spsc]") {
    struct payload {
        std::uint64_t a;
        std::uint64_t b;
        std::uint64_t c;
    };

    static_assert(std::is_trivially_copyable_v<payload>);

    spsc_ring<payload, 32> ring;
    for (std::uint64_t i = 0; i < 32; ++i) {
        REQUIRE(ring.try_push({i, i * 2, i * 3}));
    }
    REQUIRE(ring.full());
    for (std::uint64_t i = 0; i < 32; ++i) {
        payload out{};
        REQUIRE(ring.try_pop(out));
        REQUIRE(out.a == i);
        REQUIRE(out.b == i * 2);
        REQUIRE(out.c == i * 3);
    }
}

TEST_CASE("spsc_ring capacity-of-one is a degenerate but valid configuration", "[spsc]") {
    spsc_ring<std::uint64_t, 1> ring;
    REQUIRE(ring.empty());
    REQUIRE(ring.try_push(42));
    REQUIRE(ring.full());
    REQUIRE_FALSE(ring.try_push(43));
    std::uint64_t out{};
    REQUIRE(ring.try_pop(out));
    REQUIRE(out == 42);
    REQUIRE(ring.empty());
}
