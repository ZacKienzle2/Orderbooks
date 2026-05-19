#ifndef LOB_BITMAP_HPP
#define LOB_BITMAP_HPP

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lob {

// Hierarchical bit-set over [0, Ticks) with O(1) lowest_set / highest_set
// queries. Up to four cascaded 64-bit-word tiers cover Ticks up to 64^4 =
// 16'777'216. Each set / clear updates one word per active tier and short-
// circuits when the affected lower tier remains non-empty.
//
// Storage layout per side is roughly Ticks / 8 bytes for the base tier plus
// negligible upper-tier overhead. Every tier is 64-byte aligned to keep the
// hot-word for each operation isolated on its own cache line.
//
// Preconditions on caller:
//   - `bit < Ticks` for set, clear, test.
//   - Tier invariants are maintained internally; do not mutate raw words.
template <std::size_t Ticks>
class hier_bitmap {
    static constexpr std::size_t W = 64;

    [[nodiscard]] static constexpr std::size_t ceildiv(std::size_t a, std::size_t b) noexcept {
        return (a + b - 1) / b;
    }

    static constexpr std::size_t L0_W = ceildiv(Ticks, W);
    static constexpr std::size_t L1_W = (L0_W > 1) ? ceildiv(L0_W, W) : 0;
    static constexpr std::size_t L2_W = (L1_W > 1) ? ceildiv(L1_W, W) : 0;
    static constexpr std::size_t L3_W = (L2_W > 1) ? ceildiv(L2_W, W) : 0;

    static_assert(Ticks > 0, "hier_bitmap: Ticks must be positive");
    static_assert(L3_W <= 1, "hier_bitmap supports at most four tiers (Ticks <= 16'777'216)");

    static constexpr std::size_t L1_alloc = (L1_W > 0) ? L1_W : 1;
    static constexpr std::size_t L2_alloc = (L2_W > 0) ? L2_W : 1;
    static constexpr std::size_t L3_alloc = (L3_W > 0) ? L3_W : 1;

  public:
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Ticks; }

    constexpr void set(std::size_t bit) noexcept {
        assert(bit < Ticks);
        l0_[bit / W] |= mask(bit);
        if constexpr (L1_W > 0) {
            const auto l1_bit = bit / W;
            l1_[l1_bit / W] |= mask(l1_bit);
            if constexpr (L2_W > 0) {
                const auto l2_bit = l1_bit / W;
                l2_[l2_bit / W] |= mask(l2_bit);
                if constexpr (L3_W > 0) {
                    const auto l3_bit = l2_bit / W;
                    l3_[l3_bit / W] |= mask(l3_bit);
                }
            }
        }
    }

    constexpr void clear(std::size_t bit) noexcept {
        assert(bit < Ticks);
        const auto l0_word = bit / W;
        l0_[l0_word] &= ~mask(bit);
        if constexpr (L1_W > 0) {
            if (l0_[l0_word] != 0)
                return;
            const auto l1_bit = l0_word;
            const auto l1_word = l1_bit / W;
            l1_[l1_word] &= ~mask(l1_bit);
            if constexpr (L2_W > 0) {
                if (l1_[l1_word] != 0)
                    return;
                const auto l2_bit = l1_word;
                const auto l2_word = l2_bit / W;
                l2_[l2_word] &= ~mask(l2_bit);
                if constexpr (L3_W > 0) {
                    if (l2_[l2_word] != 0)
                        return;
                    const auto l3_bit = l2_word;
                    const auto l3_word = l3_bit / W;
                    l3_[l3_word] &= ~mask(l3_bit);
                }
            }
        }
    }

    [[nodiscard]] constexpr bool test(std::size_t bit) const noexcept {
        assert(bit < Ticks);
        return (l0_[bit / W] >> (bit % W)) & std::uint64_t{1};
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        if constexpr (L3_W > 0) {
            return l3_[0] == 0;
        } else if constexpr (L2_W > 0) {
            return l2_[0] == 0;
        } else if constexpr (L1_W > 0) {
            return l1_[0] == 0;
        } else {
            return l0_[0] == 0;
        }
    }

    [[nodiscard]] constexpr std::optional<std::size_t> lowest_set() const noexcept {
        if (empty())
            return std::nullopt;
        std::size_t idx = 0;
        if constexpr (L3_W > 0) {
            idx = static_cast<std::size_t>(std::countr_zero(l3_[0]));
        }
        if constexpr (L2_W > 0) {
            idx = idx * W + static_cast<std::size_t>(std::countr_zero(l2_[idx]));
        }
        if constexpr (L1_W > 0) {
            idx = idx * W + static_cast<std::size_t>(std::countr_zero(l1_[idx]));
        }
        return idx * W + static_cast<std::size_t>(std::countr_zero(l0_[idx]));
    }

    [[nodiscard]] constexpr std::optional<std::size_t> highest_set() const noexcept {
        if (empty())
            return std::nullopt;
        std::size_t idx = 0;
        if constexpr (L3_W > 0) {
            idx = (W - 1) - static_cast<std::size_t>(std::countl_zero(l3_[0]));
        }
        if constexpr (L2_W > 0) {
            idx = idx * W + ((W - 1) - static_cast<std::size_t>(std::countl_zero(l2_[idx])));
        }
        if constexpr (L1_W > 0) {
            idx = idx * W + ((W - 1) - static_cast<std::size_t>(std::countl_zero(l1_[idx])));
        }
        return idx * W + ((W - 1) - static_cast<std::size_t>(std::countl_zero(l0_[idx])));
    }

    constexpr void clear_all() noexcept {
        l0_.fill(0);
        if constexpr (L1_W > 0)
            l1_.fill(0);
        if constexpr (L2_W > 0)
            l2_.fill(0);
        if constexpr (L3_W > 0)
            l3_.fill(0);
    }

    // Lowest set bit at position >= start. Hierarchical descent: when the
    // L0 word at start's position has no set bit at or above start's offset,
    // bubble the search up to L1 (then L2, then L3 as configured), find the
    // next non-empty word in the higher tier, and walk back down to L0.
    //
    // Complexity is O(tier_count) tier transitions plus four constant-time
    // word loads (one per tier on the descent), independent of how sparse
    // the bitmap is. The previous implementation linear-scanned L0 in the
    // worst case; this version drops the engine's FOK precheck from
    // O(L0_W) to O(1) for sparsely populated books.
    [[nodiscard]] constexpr std::optional<std::size_t>
    next_set_at_or_after(std::size_t start) const noexcept {
        if (start >= Ticks)
            return std::nullopt;

        // L0 in-word probe at start.
        auto l0_word = start / W;
        const auto l0_off = start % W;
        const auto l0_masked = l0_[l0_word] & high_mask(l0_off);
        if (l0_masked != 0)
            return l0_word * W + static_cast<std::size_t>(std::countr_zero(l0_masked));

        // L0 word at start was empty above off. Bubble up through tiers.
        if constexpr (L1_W > 0) {
            const auto l1_pos = l0_word + 1;
            if (l1_pos >= L0_W)
                return std::nullopt;
            const auto l1_word = l1_pos / W;
            const auto l1_off = l1_pos % W;
            const auto l1_masked = l1_[l1_word] & high_mask(l1_off);
            if (l1_masked != 0) {
                const auto l0_idx = l1_word * W + static_cast<std::size_t>(std::countr_zero(l1_masked));
                return l0_idx * W + static_cast<std::size_t>(std::countr_zero(l0_[l0_idx]));
            }

            if constexpr (L2_W > 0) {
                const auto l2_pos = l1_word + 1;
                if (l2_pos >= L1_W)
                    return std::nullopt;
                const auto l2_word = l2_pos / W;
                const auto l2_off = l2_pos % W;
                const auto l2_masked = l2_[l2_word] & high_mask(l2_off);
                if (l2_masked != 0) {
                    const auto l1_idx = l2_word * W + static_cast<std::size_t>(std::countr_zero(l2_masked));
                    const auto l0_idx = l1_idx * W + static_cast<std::size_t>(std::countr_zero(l1_[l1_idx]));
                    return l0_idx * W + static_cast<std::size_t>(std::countr_zero(l0_[l0_idx]));
                }

                if constexpr (L3_W > 0) {
                    const auto l3_pos = l2_word + 1;
                    if (l3_pos >= L2_W)
                        return std::nullopt;
                    const auto l3_masked = l3_[0] & high_mask(l3_pos);
                    if (l3_masked == 0)
                        return std::nullopt;
                    const auto l2_idx = static_cast<std::size_t>(std::countr_zero(l3_masked));
                    const auto l1_idx = l2_idx * W + static_cast<std::size_t>(std::countr_zero(l2_[l2_idx]));
                    const auto l0_idx = l1_idx * W + static_cast<std::size_t>(std::countr_zero(l1_[l1_idx]));
                    return l0_idx * W + static_cast<std::size_t>(std::countr_zero(l0_[l0_idx]));
                }
            }
        }
        return std::nullopt;
    }

    // Highest set bit at position <= start. Mirror of next_set_at_or_after.
    [[nodiscard]] constexpr std::optional<std::size_t>
    prev_set_at_or_before(std::size_t start) const noexcept {
        if (Ticks == 0 || empty())
            return std::nullopt;
        if (start >= Ticks)
            start = Ticks - 1;

        auto l0_word = start / W;
        const auto l0_off = start % W;
        const auto l0_masked = l0_[l0_word] & low_mask_inclusive(l0_off);
        if (l0_masked != 0)
            return l0_word * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l0_masked)));

        if constexpr (L1_W > 0) {
            if (l0_word == 0)
                return std::nullopt;
            const auto l1_pos = l0_word - 1;
            const auto l1_word = l1_pos / W;
            const auto l1_off = l1_pos % W;
            const auto l1_masked = l1_[l1_word] & low_mask_inclusive(l1_off);
            if (l1_masked != 0) {
                const auto l0_idx = l1_word * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l1_masked)));
                return l0_idx * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l0_[l0_idx])));
            }

            if constexpr (L2_W > 0) {
                if (l1_word == 0)
                    return std::nullopt;
                const auto l2_pos = l1_word - 1;
                const auto l2_word = l2_pos / W;
                const auto l2_off = l2_pos % W;
                const auto l2_masked = l2_[l2_word] & low_mask_inclusive(l2_off);
                if (l2_masked != 0) {
                    const auto l1_idx = l2_word * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l2_masked)));
                    const auto l0_idx = l1_idx * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l1_[l1_idx])));
                    return l0_idx * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l0_[l0_idx])));
                }

                if constexpr (L3_W > 0) {
                    if (l2_word == 0)
                        return std::nullopt;
                    const auto l3_pos = l2_word - 1;
                    const auto l3_masked = l3_[0] & low_mask_inclusive(l3_pos);
                    if (l3_masked == 0)
                        return std::nullopt;
                    const auto l2_idx = W - 1 - static_cast<std::size_t>(std::countl_zero(l3_masked));
                    const auto l1_idx = l2_idx * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l2_[l2_idx])));
                    const auto l0_idx = l1_idx * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l1_[l1_idx])));
                    return l0_idx * W + (W - 1 - static_cast<std::size_t>(std::countl_zero(l0_[l0_idx])));
                }
            }
        }
        return std::nullopt;
    }

  private:
    [[nodiscard]] static constexpr std::uint64_t mask(std::size_t bit) noexcept {
        return std::uint64_t{1} << (bit % W);
    }

    // Bits at position `off` and above within a 64-bit word.
    [[nodiscard]] static constexpr std::uint64_t high_mask(std::size_t off) noexcept {
        return ~std::uint64_t{0} << off;
    }

    // Bits at position `off` and below within a 64-bit word (inclusive of off).
    [[nodiscard]] static constexpr std::uint64_t low_mask_inclusive(std::size_t off) noexcept {
        return (off == W - 1) ? ~std::uint64_t{0}
                              : ((std::uint64_t{1} << (off + 1)) - 1);
    }

    alignas(64) std::array<std::uint64_t, L0_W> l0_{};
    alignas(64) std::array<std::uint64_t, L1_alloc> l1_{};
    alignas(64) std::array<std::uint64_t, L2_alloc> l2_{};
    alignas(64) std::array<std::uint64_t, L3_alloc> l3_{};
};

}  // namespace lob

#endif  // LOB_BITMAP_HPP
