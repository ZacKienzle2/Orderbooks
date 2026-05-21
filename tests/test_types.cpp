#include <lob/types.hpp>

#include <cstdint>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("primitive type widths", "[types]") {
    STATIC_REQUIRE(sizeof(lob::tick_t) == 4U);
    STATIC_REQUIRE(sizeof(lob::qty_t) == 8U);
    STATIC_REQUIRE(sizeof(lob::order_id_t) == 8U);
    STATIC_REQUIRE(sizeof(lob::seq_t) == 8U);
}

TEST_CASE("side and tif fit in one byte", "[types]") {
    STATIC_REQUIRE(sizeof(lob::side) == 1U);
    STATIC_REQUIRE(sizeof(lob::tif) == 1U);
    STATIC_REQUIRE(std::is_trivial_v<lob::side>);
    STATIC_REQUIRE(std::is_trivial_v<lob::tif>);
}

TEST_CASE("opposite flips bid and ask", "[types]") {
    STATIC_REQUIRE(lob::opposite(lob::side::bid) == lob::side::ask);
    STATIC_REQUIRE(lob::opposite(lob::side::ask) == lob::side::bid);
}
