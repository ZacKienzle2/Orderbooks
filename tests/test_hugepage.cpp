#include <lob/hugepage.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("hugepage_region provides a usable, correctly sized region", "[hugepage]") {
    constexpr std::size_t bytes = std::size_t{4} << 20;  // 4 MiB spans two huge pages
    const lob::hugepage_region region{bytes, 64};

    REQUIRE(region.data() != nullptr);
    REQUIRE(region.size() == bytes);
    // Whichever backing the host allowed, it is never 'none' after construction.
    REQUIRE(region.source() != lob::hugepage_region::backing::none);

    // Write a byte into every 4 KiB page and read it back to prove the whole
    // span is mapped and writable.
    auto* p = static_cast<std::uint8_t*>(region.data());
    for (std::size_t i = 0; i < bytes; i += 4096) {
        p[i] = static_cast<std::uint8_t>(i);
    }
    for (std::size_t i = 0; i < bytes; i += 4096) {
        REQUIRE(p[i] == static_cast<std::uint8_t>(i));
    }
}

TEST_CASE("hugepage_region honours a sub-page request", "[hugepage]") {
    const lob::hugepage_region region{128, 64};
    REQUIRE(region.data() != nullptr);
    REQUIRE(region.size() == 128);
}

TEST_CASE("hugepage_region transfers ownership on move", "[hugepage]") {
    lob::hugepage_region a{std::size_t{1} << 20, 64};
    void* const original = a.data();
    const auto original_backing = a.source();

    const lob::hugepage_region b{std::move(a)};
    REQUIRE(b.data() == original);
    REQUIRE(b.source() == original_backing);
    // The moved-from region is left empty so its destructor never double-frees.
    // NOLINTNEXTLINE(hicpp-invalid-access-moved,bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    REQUIRE(a.data() == nullptr);
}
