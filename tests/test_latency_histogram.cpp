#include <lob/latency_histogram.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("latency_histogram reports percentiles of a uniform distribution", "[histogram]") {
    lob::latency_histogram h{1'000'000, 3};
    for (std::uint64_t v = 1; v <= 100'000; ++v) {
        h.record(v);
    }

    REQUIRE(h.count() == 100'000);
    REQUIRE(h.min() == 1);
    REQUIRE(h.max() == 100'000);

    // Three significant figures bound the relative error to 0.1 percent, so
    // each reported percentile sits within a small window of its true value.
    const auto p50 = h.value_at_percentile(50.0);
    REQUIRE(p50 >= 49'500);
    REQUIRE(p50 <= 50'500);

    const auto p99 = h.value_at_percentile(99.0);
    REQUIRE(p99 >= 98'500);
    REQUIRE(p99 <= 99'500);

    const auto p999 = h.value_at_percentile(99.9);
    REQUIRE(p999 >= 99'700);
    REQUIRE(p999 <= 100'000);

    REQUIRE(h.mean() > 49'000.0);
    REQUIRE(h.mean() < 51'000.0);
}

TEST_CASE("latency_histogram reports a single-valued distribution tightly", "[histogram]") {
    lob::latency_histogram h{10'000, 3};
    for (int i = 0; i < 1'000; ++i) {
        h.record(100);
    }
    REQUIRE(h.count() == 1'000);
    REQUIRE(h.min() == 100);
    REQUIRE(h.max() == 100);
    REQUIRE(h.value_at_percentile(50.0) >= 100);
    REQUIRE(h.value_at_percentile(99.9) <= 101);
}

TEST_CASE("latency_histogram clamps values above the trackable maximum", "[histogram]") {
    lob::latency_histogram h{1'000, 2};
    h.record(5'000);
    REQUIRE(h.count() == 1);
    REQUIRE(h.max() == 1'000);
    REQUIRE(h.value_at_percentile(100.0) >= 1'000);
}

TEST_CASE("latency_histogram reset clears all state", "[histogram]") {
    lob::latency_histogram h{1'000, 2};
    h.record(500);
    h.record(600);
    REQUIRE(h.count() == 2);
    h.reset();
    REQUIRE(h.count() == 0);
    REQUIRE(h.value_at_percentile(50.0) == 0);
    REQUIRE(h.mean() <= 0.0);
}
