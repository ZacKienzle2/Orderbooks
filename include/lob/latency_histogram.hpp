#ifndef LOB_LATENCY_HISTOGRAM_HPP
#define LOB_LATENCY_HISTOGRAM_HPP

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lob {

// High-dynamic-range histogram for latency samples, in the style of Gil Tene's
// HdrHistogram.
//
// A flat histogram cannot hold both the resolution and the range a latency
// distribution needs: nanosecond resolution near the floor and a tail that
// reaches milliseconds would take billions of linear buckets. The HDR scheme
// instead lays out buckets by magnitude. Each power-of-two band is split into
// a fixed number of linear sub-buckets, so the relative error is bounded to
// the requested significant figures across the whole range while the storage
// stays a few tens of kilobytes.
//
// record() is O(1) and allocation-free: it derives the bucket and sub-bucket
// from the value's bit width and bumps one counter, so it is cheap enough to
// call on every operation under measurement. Percentile queries walk the
// counters once. The histogram is single-threaded; give each measured thread
// its own and merge offline if needed.
class latency_histogram {
  public:
    // highest_trackable_value caps the recordable range; larger samples are
    // clamped to it. significant_figures (1 to 5) sets the relative precision,
    // so 3 keeps every reported value within 0.1 percent of the true value.
    latency_histogram(std::uint64_t highest_trackable_value, unsigned significant_figures) {
        const auto largest_single_unit = 2 * pow10_(significant_figures);
        sub_bucket_count_magnitude_ =
            static_cast<unsigned>(std::ceil(std::log2(static_cast<double>(largest_single_unit))));
        sub_bucket_half_count_magnitude_ = sub_bucket_count_magnitude_ - 1;
        sub_bucket_count_ = std::uint64_t{1} << sub_bucket_count_magnitude_;
        sub_bucket_half_count_ = sub_bucket_count_ >> 1;
        sub_bucket_mask_ = sub_bucket_count_ - 1;
        leading_zero_count_base_ = 64U - sub_bucket_count_magnitude_;

        highest_ = highest_trackable_value < sub_bucket_count_ ? sub_bucket_count_
                                                               : highest_trackable_value;
        bucket_count_ = buckets_needed_(highest_);
        const std::size_t len = (bucket_count_ + 1) * sub_bucket_half_count_;
        counts_.assign(len, 0);
    }

    void record(std::uint64_t value) noexcept {
        value = std::min(value, highest_);
        ++counts_[counts_index_(value)];
        ++total_;
        min_ = std::min(min_, value);
        max_ = std::max(max_, value);
    }

    void reset() noexcept {
        for (auto& c : counts_) {
            c = 0;
        }
        total_ = 0;
        min_ = UINT64_MAX;
        max_ = 0;
    }

    [[nodiscard]] std::uint64_t count() const noexcept { return total_; }

    [[nodiscard]] std::uint64_t min() const noexcept { return total_ == 0 ? 0 : min_; }

    [[nodiscard]] std::uint64_t max() const noexcept { return max_; }

    [[nodiscard]] double mean() const noexcept {
        if (total_ == 0) {
            return 0.0;
        }
        double weighted = 0.0;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            if (counts_[i] != 0) {
                weighted += static_cast<double>(counts_[i]) *
                            static_cast<double>(median_equivalent_(value_from_index_(i)));
            }
        }
        return weighted / static_cast<double>(total_);
    }

    // The smallest recorded value at or below which the given percentile of
    // samples fall. percentile is in [0, 100]; 99.9 asks for the p99.9 tail.
    [[nodiscard]] std::uint64_t value_at_percentile(double percentile) const noexcept {
        if (total_ == 0) {
            return 0;
        }
        const double clamped = std::clamp(percentile, 0.0, 100.0);
        auto target =
            static_cast<std::uint64_t>(std::ceil((clamped / 100.0) * static_cast<double>(total_)));
        if (target == 0) {
            target = 1;
        }
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            seen += counts_[i];
            if (seen >= target) {
                return highest_equivalent_(value_from_index_(i));
            }
        }
        return max_;
    }

  private:
    static std::uint64_t pow10_(unsigned n) noexcept {
        std::uint64_t r = 1;
        for (unsigned i = 0; i < n; ++i) {
            r *= 10;
        }
        return r;
    }

    [[nodiscard]] unsigned bucket_index_(std::uint64_t value) const noexcept {
        const auto clz = static_cast<unsigned>(std::countl_zero(value | sub_bucket_mask_));
        return leading_zero_count_base_ - clz;
    }

    [[nodiscard]] static std::uint64_t sub_bucket_index_(std::uint64_t value,
                                                         unsigned bucket_index) noexcept {
        return value >> bucket_index;
    }

    [[nodiscard]] std::size_t counts_index_(std::uint64_t value) const noexcept {
        const auto bucket = bucket_index_(value);
        const auto sub_bucket = sub_bucket_index_(value, bucket);
        const std::uint64_t bucket_base = (static_cast<std::uint64_t>(bucket) + 1)
                                          << sub_bucket_half_count_magnitude_;
        return bucket_base + sub_bucket - sub_bucket_half_count_;
    }

    [[nodiscard]] std::uint64_t value_from_index_(std::size_t index) const noexcept {
        auto bucket = static_cast<std::int64_t>(index >> sub_bucket_half_count_magnitude_) - 1;
        auto sub_bucket = static_cast<std::int64_t>((index & (sub_bucket_half_count_ - 1)) +
                                                    sub_bucket_half_count_);
        if (bucket < 0) {
            sub_bucket -= static_cast<std::int64_t>(sub_bucket_half_count_);
            bucket = 0;
        }
        return static_cast<std::uint64_t>(sub_bucket) << static_cast<unsigned>(bucket);
    }

    [[nodiscard]] std::uint64_t size_of_equivalent_range_(std::uint64_t value) const noexcept {
        const auto bucket = bucket_index_(value);
        const auto sub_bucket = sub_bucket_index_(value, bucket);
        const unsigned adjusted = sub_bucket >= sub_bucket_count_ ? bucket + 1 : bucket;
        return std::uint64_t{1} << adjusted;
    }

    [[nodiscard]] std::uint64_t lowest_equivalent_(std::uint64_t value) const noexcept {
        const auto bucket = bucket_index_(value);
        const auto sub_bucket = sub_bucket_index_(value, bucket);
        return sub_bucket << bucket;
    }

    [[nodiscard]] std::uint64_t highest_equivalent_(std::uint64_t value) const noexcept {
        return lowest_equivalent_(value) + size_of_equivalent_range_(value) - 1;
    }

    [[nodiscard]] std::uint64_t median_equivalent_(std::uint64_t value) const noexcept {
        return lowest_equivalent_(value) + (size_of_equivalent_range_(value) >> 1);
    }

    [[nodiscard]] unsigned buckets_needed_(std::uint64_t value) const noexcept {
        std::uint64_t smallest_untrackable = sub_bucket_count_;
        unsigned buckets = 1;
        while (smallest_untrackable <= value) {
            if (smallest_untrackable > (UINT64_MAX >> 1)) {
                return buckets + 1;
            }
            smallest_untrackable <<= 1;
            ++buckets;
        }
        return buckets;
    }

    std::vector<std::uint64_t> counts_;
    std::uint64_t highest_{0};
    std::uint64_t total_{0};
    std::uint64_t min_{UINT64_MAX};
    std::uint64_t max_{0};
    std::uint64_t sub_bucket_count_{0};
    std::uint64_t sub_bucket_half_count_{0};
    std::uint64_t sub_bucket_mask_{0};
    unsigned sub_bucket_count_magnitude_{0};
    unsigned sub_bucket_half_count_magnitude_{0};
    unsigned leading_zero_count_base_{0};
    unsigned bucket_count_{0};
};

}  // namespace lob

#endif  // LOB_LATENCY_HISTOGRAM_HPP
