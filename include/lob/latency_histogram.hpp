#ifndef LOB_LATENCY_HISTOGRAM_HPP
#define LOB_LATENCY_HISTOGRAM_HPP

#include <hdr/hdr_histogram.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>

namespace lob {

// Latency samples in an HdrHistogram, from HdrHistogram_c (ADR-0045). Each
// power-of-two band of values is split into linear sub-buckets, so the
// relative error stays within the requested significant figures across the
// whole range in a few tens of kilobytes, and record() is O(1) and
// allocation-free.
//
// This owns the histogram, closing it with the object, and adds one policy the
// library leaves to its caller: a sample above highest_trackable_value is
// clamped to it and counted as overflow, so a tail that escaped the range shows
// instead of vanishing. min() and max() are the exact recorded extremes, and
// percentiles are the highest value equivalent to the bucket they fall in.
// Single-threaded; give each measured thread its own.
class latency_histogram {
   public:
    // significant_figures is 1 to 5, so 3 keeps every reported value within
    // 0.1 percent of the true value.
    latency_histogram(std::uint64_t highest_trackable_value, unsigned significant_figures)
        : cap_(highest_trackable_value < 2 ? 2 : highest_trackable_value) {
        hdr_histogram* h = nullptr;
        const int rc =
            hdr_init(1, static_cast<std::int64_t>(cap_), static_cast<int>(significant_figures), &h);
        if (rc == ENOMEM)
            throw std::bad_alloc{};
        if (rc != 0)
            throw std::invalid_argument{"latency_histogram: significant_figures must be 1 to 5"};
        h_.reset(h);
    }

    void record(std::uint64_t value) noexcept {
        if (value > cap_) [[unlikely]] {
            ++overflow_;
            value = cap_;
        }
        (void)hdr_record_value(h_.get(), static_cast<std::int64_t>(value));
    }

    void reset() noexcept {
        hdr_reset(h_.get());
        overflow_ = 0;
    }

    [[nodiscard]] std::uint64_t count() const noexcept {
        return static_cast<std::uint64_t>(h_->total_count);
    }

    // Samples that exceeded highest_trackable_value and were clamped to it.
    // Nonzero means max() and the upper percentiles understate the true tail.
    [[nodiscard]] std::uint64_t overflow_count() const noexcept { return overflow_; }

    [[nodiscard]] std::uint64_t min() const noexcept {
        return count() == 0 ? 0 : static_cast<std::uint64_t>(h_->min_value);
    }

    [[nodiscard]] std::uint64_t max() const noexcept {
        return static_cast<std::uint64_t>(h_->max_value);
    }

    [[nodiscard]] double mean() const noexcept { return count() == 0 ? 0.0 : hdr_mean(h_.get()); }

    // The value at or below which the given percentile of samples fall, for
    // percentile in [0, 100]; 99.9 asks for the p99.9 tail.
    [[nodiscard]] std::uint64_t value_at_percentile(double percentile) const noexcept {
        if (count() == 0)
            return 0;
        return static_cast<std::uint64_t>(hdr_value_at_percentile(h_.get(), percentile));
    }

   private:
    struct closer {
        void operator()(hdr_histogram* h) const noexcept { hdr_close(h); }
    };

    std::unique_ptr<hdr_histogram, closer> h_;
    std::uint64_t cap_;
    std::uint64_t overflow_{0};
};

}  // namespace lob

#endif  // LOB_LATENCY_HISTOGRAM_HPP
