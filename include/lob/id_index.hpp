#ifndef LOB_ID_INDEX_HPP
#define LOB_ID_INDEX_HPP

#include <lob/order.hpp>
#include <lob/types.hpp>

#include <cstddef>

#include <ankerl/unordered_dense.h>

namespace lob {

// Thin wrapper over ankerl::unordered_dense::segmented_map<order_id_t,
// order*>. Segmented backing avoids the giant single allocation on rehash
// spikes that the flat variant exhibits under bursty submit traffic.
//
// The map borrows pointers into the slab arena that allocated the orders;
// it never owns the storage. Insert / erase are O(1) amortised, lookup is
// one or two cache-line probes typical.
class id_index {
    using map_type = ankerl::unordered_dense::
        segmented_map<order_id_t, order*, ankerl::unordered_dense::hash<order_id_t>>;

  public:
    id_index() = default;

    explicit id_index(std::size_t initial_capacity) { map_.reserve(initial_capacity); }

    void insert(order_id_t id, order* p) noexcept { map_.emplace(id, p); }

    [[nodiscard]] order* lookup(order_id_t id) const noexcept {
        const auto it = map_.find(id);
        return (it == map_.end()) ? nullptr : it->second;
    }

    void erase(order_id_t id) noexcept { map_.erase(id); }

    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

    [[nodiscard]] bool empty() const noexcept { return map_.empty(); }

    void clear() noexcept { map_.clear(); }

  private:
    map_type map_;
};

}  // namespace lob

#endif  // LOB_ID_INDEX_HPP
