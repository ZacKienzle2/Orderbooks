#ifndef LOB_ID_INDEX_HPP
#define LOB_ID_INDEX_HPP

#include <lob/order.hpp>
#include <lob/types.hpp>

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace lob {

// Open-addressed SoA hash table mapping order_id_t to order*.
//
// Layout: parallel key and value vectors sized to the next power of two
// above 2 * capacity, keeping the load factor at or below 0.5 and the
// expected probe length around 1.5. The key array is initialised to
// std::numeric_limits<order_id_t>::max() which acts as the empty
// sentinel; that single id value is reserved and must not be inserted.
//
// Hash: SplitMix64 of the id, masked by (capacity - 1). Linear probing
// resolves collisions. Erase uses backward-shift deletion to keep probe
// sequences tight without tombstones.
//
// Thread safety: single-threaded by contract. insert, lookup, erase, and
// clear share mutable storage with no synchronisation. The engine drives
// the index on a single shard thread; cross-thread access is undefined.
//
// insert with a key already present overwrites the stored pointer. The
// engine never inserts duplicates; the overwrite path is defensive only.
//
// All operations are noexcept and allocation-free on the hot path; the
// only allocation occurs in the constructor when the storage vectors
// are sized. See ADR-0017 for the rationale.
class id_index {
    static constexpr order_id_t empty_key = std::numeric_limits<order_id_t>::max();

    [[nodiscard]] static constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    [[nodiscard]] static constexpr std::size_t round_up_pow2(std::size_t n) noexcept {
        if (n <= 1)
            return 1;
        return std::size_t{1} << (64 - std::countl_zero(n - 1));
    }

  public:
    id_index() : id_index(default_capacity_) {}

    explicit id_index(std::size_t capacity_hint) {
        const std::size_t want = capacity_hint == 0 ? default_capacity_ : capacity_hint;
        // Load factor target 0.5: round 2 * want up to the next power of two.
        const std::size_t cap = round_up_pow2(want * 2);
        keys_.assign(cap, empty_key);
        values_.assign(cap, nullptr);
        mask_ = cap - 1;
    }

    void insert(order_id_t id, order* p) noexcept {
        assert(id != empty_key && "id_index: sentinel id is reserved");
        assert(size_ < (mask_ + 1) / 2 && "id_index: load factor invariant violated");
        std::size_t i = splitmix64(id) & mask_;
        while (true) {
            const auto k = keys_[i];
            if (k == empty_key) {
                keys_[i] = id;
                values_[i] = p;
                ++size_;
                return;
            }
            if (k == id) {
                values_[i] = p;
                return;
            }
            i = (i + 1) & mask_;
        }
    }

    [[nodiscard]] order* lookup(order_id_t id) const noexcept {
        std::size_t i = splitmix64(id) & mask_;
        while (true) {
            const auto k = keys_[i];
            if (k == id) [[likely]]
                return values_[i];
            if (k == empty_key)
                return nullptr;
            i = (i + 1) & mask_;
        }
    }

    void erase(order_id_t id) noexcept {
        assert(id != empty_key && "id_index: sentinel id is reserved");
        std::size_t i = splitmix64(id) & mask_;
        while (true) {
            const auto k = keys_[i];
            if (k == empty_key)
                return;
            if (k == id) {
                shift_back_from_(i);
                --size_;
                return;
            }
            i = (i + 1) & mask_;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    void clear() noexcept {
        std::fill(keys_.begin(), keys_.end(), empty_key);
        std::fill(values_.begin(), values_.end(), nullptr);
        size_ = 0;
    }

  private:
    // Backward-shift deletion: starting from the freshly emptied slot,
    // walk forward and pull back any entry whose preferred bucket sits
    // at or before the emptied slot (modulo wrap-around), repeating
    // until reaching an empty slot. Keeps probe sequences tight; no
    // tombstones required.
    void shift_back_from_(std::size_t hole) noexcept {
        std::size_t j = (hole + 1) & mask_;
        while (true) {
            const auto k = keys_[j];
            if (k == empty_key) {
                keys_[hole] = empty_key;
                values_[hole] = nullptr;
                return;
            }
            const std::size_t home = splitmix64(k) & mask_;
            // Distance from home to hole vs home to j; if hole is closer
            // (along the linear-probe direction), pull this entry back.
            const std::size_t hole_dist = (hole - home) & mask_;
            const std::size_t j_dist = (j - home) & mask_;
            if (hole_dist < j_dist) {
                keys_[hole] = k;
                values_[hole] = values_[j];
                hole = j;
            }
            j = (j + 1) & mask_;
        }
    }

    static constexpr std::size_t default_capacity_ = 256;

    std::vector<order_id_t> keys_{};
    std::vector<order*> values_{};
    std::size_t mask_{0};
    std::size_t size_{0};
};

}  // namespace lob

#endif  // LOB_ID_INDEX_HPP
