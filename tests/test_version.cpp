#include <lob/version.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version is populated by build system", "[version]") {
    REQUIRE(lob::version::major() == 0U);
    REQUIRE(lob::version::minor() == 1U);
    REQUIRE(lob::version::patch() == 0U);
    REQUIRE_FALSE(lob::version::string().empty());
    REQUIRE_FALSE(lob::version::git_sha().empty());
}
