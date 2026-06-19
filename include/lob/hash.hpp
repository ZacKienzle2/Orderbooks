#ifndef LOB_HASH_HPP
#define LOB_HASH_HPP

#include <lob/types.hpp>

#include <cstddef>
#include <cstdint>

namespace lob {

// SplitMix64 finaliser. One multiply and two xor-shifts per call with full
// avalanche, so contiguous key ranges spread evenly across a power-of-two
// modulus. The function is stateless and identical on every host, which is
// what keeps shard assignment deterministic across runs and snapshots.
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Map a symbol id to one of num_shards buckets. num_shards must be a power of
// two so the modulus reduces to a single mask. SplitMix64 spreads the low
// bits as well as the high bits, so masking off the high bits loses no
// distribution quality.
[[nodiscard]] constexpr std::size_t shard_index(symbol_id_t sym, std::size_t num_shards) noexcept {
    return static_cast<std::size_t>(splitmix64(sym) & (num_shards - 1));
}

}  // namespace lob

#endif  // LOB_HASH_HPP
