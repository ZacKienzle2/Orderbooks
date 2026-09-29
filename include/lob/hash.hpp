#ifndef LOB_HASH_HPP
#define LOB_HASH_HPP

#include <lob/types.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace lob {

namespace detail {

// The shifts and multipliers of mix64variant13, David Stafford's Mix13 variant
// of the MurmurHash3 finaliser, as Steele, Lea and Flood list it in figure 16
// of "Fast splittable pseudorandom number generators" (OOPSLA 2014,
// doi:10.1145/2660193.2660195).
inline constexpr unsigned mix13_shift_1 = 30;
inline constexpr std::uint64_t mix13_multiplier_1 = 0xBF58476D1CE4E5B9ULL;
inline constexpr unsigned mix13_shift_2 = 27;
inline constexpr std::uint64_t mix13_multiplier_2 = 0x94D049BB133111EBULL;
inline constexpr unsigned mix13_shift_3 = 31;

}  // namespace detail

// SplitMix64 finaliser, mix64variant13. Two multiplies and three xor-shifts
// per call with full avalanche, so contiguous key ranges spread evenly across
// a power-of-two modulus. The function is stateless and identical on every
// host, which is what keeps shard assignment deterministic across runs and
// snapshots.
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x = (x ^ (x >> detail::mix13_shift_1)) * detail::mix13_multiplier_1;
    x = (x ^ (x >> detail::mix13_shift_2)) * detail::mix13_multiplier_2;
    return x ^ (x >> detail::mix13_shift_3);
}

// Map a symbol id to one of num_shards buckets. num_shards must be a power of
// two so the modulus reduces to a single mask. SplitMix64 spreads the low
// bits as well as the high bits, so masking off the high bits loses no
// distribution quality.
[[nodiscard]] constexpr std::size_t shard_index(symbol_id_t sym, std::size_t num_shards) noexcept {
    return splitmix64(sym) & (num_shards - 1);
}

// Seed for shard i's engine sequence counter. Every shard counting from zero
// collides the seq stamps the moment their event streams merge, so shard i
// takes the disjoint range [i * 2^(64 - log2(N)), (i + 1) * 2^(64 - log2(N)))
// and a merged stream keeps globally unique, per-shard-monotonic stamps.
// num_shards must be a power of two, matching shard_index.
[[nodiscard]] constexpr seq_t shard_seq_base(std::size_t shard_idx,
                                             std::size_t num_shards) noexcept {
    if (num_shards <= 1)
        return 0;
    const int log2_shards = std::countr_zero(num_shards);
    return static_cast<seq_t>(shard_idx) << (std::numeric_limits<seq_t>::digits - log2_shards);
}

}  // namespace lob

#endif  // LOB_HASH_HPP
