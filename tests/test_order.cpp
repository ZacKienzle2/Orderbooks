#include <lob/order.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("order is exactly one cache line", "[order]") {
    STATIC_REQUIRE(sizeof(lob::order) == 64);
    STATIC_REQUIRE(alignof(lob::order) == 64);
}

TEST_CASE("order FIFO link / unlink", "[order]") {
    lob::order a{}, b{}, c{};
    a.id = 1;
    b.id = 2;
    c.id = 3;

    lob::order_fifo fifo;
    fifo.push_back(a);
    fifo.push_back(b);
    fifo.push_back(c);

    REQUIRE(fifo.size() == 3);
    REQUIRE(fifo.front().id == 1);
    REQUIRE(fifo.back().id == 3);

    fifo.pop_front();
    REQUIRE(fifo.size() == 2);
    REQUIRE(fifo.front().id == 2);

    fifo.erase(fifo.iterator_to(b));
    REQUIRE(fifo.size() == 1);
    REQUIRE(fifo.front().id == 3);

    fifo.clear();
    REQUIRE(fifo.empty());
}
