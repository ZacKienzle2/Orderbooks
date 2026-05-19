#ifndef LOB_ARENA_HPP
#define LOB_ARENA_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace lob {

// Fixed-capacity slab allocator over T. Storage is a single contiguous,
// cache-line-aligned heap block of `Capacity` slots, each sized and aligned
// to fit a T. Free slots are chained through their own storage via an
// intrusive next pointer (no per-allocation bookkeeping).
//
// allocate() and deallocate() are O(1), branchless except for the empty /
// full guard, and noexcept. The arena does not construct or destroy T; the
// caller manages object lifetime (typical use: T is trivially destructible
// and the caller assigns or placement-news into the slot).
//
// Single-threaded by design. Wrap in a synchronisation primitive only if
// cross-thread access is required (defeats the purpose for hot-path use).
template <class T, std::size_t Capacity>
class slab_arena {
    static_assert(std::is_trivially_destructible_v<T>,
                  "slab_arena requires trivially-destructible T");
    static_assert(sizeof(T) >= sizeof(void*),
                  "slab_arena requires T to be at least pointer-sized so the "
                  "freelist link can be overlaid on a free slot");
    static_assert(Capacity > 0, "slab_arena requires positive Capacity");

    struct alignas(T) slot {
        std::byte bytes[sizeof(T)];
    };  // NOLINT

    using slot_array = std::array<slot, Capacity>;

  public:
    slab_arena() {
        storage_ = std::make_unique<slot_array>();
        // Initialise freelist: each slot's first word points to the next slot.
        for (std::size_t i = 0; i + 1 < Capacity; ++i) {
            store_link_(&(*storage_)[i], &(*storage_)[i + 1]);
        }
        store_link_(&(*storage_)[Capacity - 1], nullptr);
        free_head_ = &(*storage_)[0];
    }

    slab_arena(slab_arena&&) noexcept = default;
    slab_arena& operator=(slab_arena&&) noexcept = default;
    slab_arena(const slab_arena&) = delete;
    slab_arena& operator=(const slab_arena&) = delete;
    ~slab_arena() = default;

    [[nodiscard]] T* allocate() noexcept {
        if (free_head_ == nullptr) [[unlikely]]
            return nullptr;
        slot* s = free_head_;
        free_head_ = load_link_(s);
        ++in_use_;
        return std::launder(reinterpret_cast<T*>(s));
    }

    void deallocate(T* p) noexcept {
        if (p == nullptr) [[unlikely]]
            return;
        auto* s = reinterpret_cast<slot*>(p);
        store_link_(s, free_head_);
        free_head_ = s;
        --in_use_;
    }

    [[nodiscard]] std::size_t in_use() const noexcept { return in_use_; }

    [[nodiscard]] std::size_t capacity() const noexcept { return Capacity; }

    [[nodiscard]] bool empty() const noexcept { return in_use_ == 0; }

    [[nodiscard]] bool full() const noexcept { return in_use_ == Capacity; }

    [[nodiscard]] bool owns(const T* p) const noexcept {
        const auto* s = reinterpret_cast<const slot*>(p);
        const auto* base = (*storage_).data();
        return s >= base && s < base + Capacity;
    }

  private:
    static void store_link_(slot* s, slot* next) noexcept {
        std::memcpy(s->bytes, &next, sizeof(next));
    }

    static slot* load_link_(slot* s) noexcept {
        slot* next{nullptr};
        std::memcpy(&next, s->bytes, sizeof(next));
        return next;
    }

    alignas(64) std::unique_ptr<slot_array> storage_;
    slot* free_head_{nullptr};
    std::size_t in_use_{0};
};

}  // namespace lob

#endif  // LOB_ARENA_HPP
