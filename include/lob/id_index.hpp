#ifndef LOB_ID_INDEX_HPP
#define LOB_ID_INDEX_HPP

#include <lob/order.hpp>
#include <lob/types.hpp>

#include <boost/unordered/unordered_flat_map.hpp>

#include <cstddef>

namespace lob {

// Maps order_id_t to the order* resting in the arena, over
// boost::unordered_flat_map, an open-addressed table that probes a group of
// fifteen slots with one SIMD compare of their metadata (ADR-0043). This class
// adds only the engine's vocabulary. take() removes an id and returns what it
// named in one probe, and the table is reserved for the engine's capacity at
// construction, so no operation on the hot path rehashes or allocates.
//
// Single-threaded by contract, like the engine that owns it.
class id_index {
   public:
    id_index() : id_index(default_capacity_) {}

    explicit id_index(std::size_t capacity_hint) {
        map_.reserve(capacity_hint == 0 ? default_capacity_ : capacity_hint);
    }

    // The engine never inserts an id twice; a repeated insert overwrites.
    void insert(order_id_t id, order* p) noexcept { map_.insert_or_assign(id, p); }

    [[nodiscard]] order* lookup(order_id_t id) const noexcept {
        const auto it = map_.find(id);
        return it == map_.end() ? nullptr : it->second;
    }

    // Remove id and return the order it named, or nullptr when absent.
    [[nodiscard]] order* take(order_id_t id) noexcept {
        const auto it = map_.find(id);
        if (it == map_.end())
            return nullptr;
        order* p = it->second;
        map_.erase(it);
        return p;
    }

    void erase(order_id_t id) noexcept { map_.erase(id); }

    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

    [[nodiscard]] bool empty() const noexcept { return map_.empty(); }

    void clear() noexcept { map_.clear(); }

   private:
    static constexpr std::size_t default_capacity_ = 256;

    boost::unordered_flat_map<order_id_t, order*> map_;
};

}  // namespace lob

#endif  // LOB_ID_INDEX_HPP
