#include <lob/level.hpp>
#include <lob/order.hpp>

#include <array>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("level default state is empty with zero aggregate", "[level]") {
    lob::level lvl;
    REQUIRE(lvl.empty());
    REQUIRE(lvl.order_count() == 0);
    REQUIRE(lvl.aggregate == 0);
}

TEST_CASE("level push_back tracks aggregate", "[level]") {
    lob::level lvl;
    std::array<lob::order, 3> orders{};
    orders[0].id = 1;
    orders[0].remaining = 10;
    orders[1].id = 2;
    orders[1].remaining = 20;
    orders[2].id = 3;
    orders[2].remaining = 30;

    for (auto& o : orders)
        lvl.push_back(o);

    REQUIRE(lvl.order_count() == 3);
    REQUIRE(lvl.aggregate == 60);
    REQUIRE(lvl.fifo.front().id == 1);
    REQUIRE(lvl.fifo.back().id == 3);
}

TEST_CASE("level unlink preserves aggregate and FIFO order", "[level]") {
    lob::level lvl;
    std::array<lob::order, 3> orders{};
    orders[0].id = 1;
    orders[0].remaining = 10;
    orders[1].id = 2;
    orders[1].remaining = 20;
    orders[2].id = 3;
    orders[2].remaining = 30;
    for (auto& o : orders)
        lvl.push_back(o);

    lvl.unlink(orders[1]);
    REQUIRE(lvl.order_count() == 2);
    REQUIRE(lvl.aggregate == 40);
    REQUIRE(lvl.fifo.front().id == 1);
    REQUIRE(lvl.fifo.back().id == 3);

    lvl.unlink(orders[0]);
    REQUIRE(lvl.fifo.front().id == 3);

    lvl.unlink(orders[2]);
    REQUIRE(lvl.empty());
    REQUIRE(lvl.aggregate == 0);
}

TEST_CASE("level clear resets state", "[level]") {
    lob::level lvl;
    std::array<lob::order, 2> orders{};
    orders[0].remaining = 5;
    orders[1].remaining = 15;
    for (auto& o : orders)
        lvl.push_back(o);
    REQUIRE(lvl.aggregate == 20);
    lvl.clear();
    REQUIRE(lvl.empty());
    REQUIRE(lvl.aggregate == 0);
}
