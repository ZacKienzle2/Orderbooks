#ifndef LOB_ARENA_HPP
#define LOB_ARENA_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
    // T's destructor is invoked explicitly on deallocate. Trivial destructors
    // (the common case) compile to nothing, so the cost is zero for PODs; for
    // T like lob::order whose Boost.Intrusive hook libstdc++ does not regard
    // as trivially destructible (libc++ does), explicit destruction keeps the
    // arena safe without a platform-dependent static_assert.
    static_assert(sizeof(T) >= sizeof(void*),
                  "slab_arena requires T to be at least pointer-sized so the "
                  "freelist link can be overlaid on a free slot");
    static_assert(Capacity > 0, "slab_arena requires positive Capacity");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "slab_arena::deallocate is noexcept and invokes ~T(); the contract "
                  "requires T's destructor to be noexcept to prevent std::terminate");

    struct alignas(T) slot {
        std::byte bytes[sizeof(T)];
    };  // NOLINT

    using slot_array = std::array<slot, Capacity>;

  public:
    // The constructor reserves the slab storage but deliberately leaves
    // the intrusive freelist uninitialised. The first allocate() call
    // builds the freelist on the consuming thread, which is the thread
    // that pays the page-fault cost; Linux's first-touch NUMA policy
    // then binds every slab page to that thread's NUMA node. Without
    // this deferral the freelist would be built on the router thread
    // and every subsequent allocate/deallocate on the consumer thread
    // would pay a cross-socket access. See ADR-0016 for the rationale.
    slab_arena() { storage_ = std::make_unique<slot_array>(); }

    slab_arena(slab_arena&&) noexcept = default;
    slab_arena& operator=(slab_arena&&) noexcept = default;
    slab_arena(const slab_arena&) = delete;
    slab_arena& operator=(const slab_arena&) = delete;
    ~slab_arena() = default;

    [[nodiscard]] T* allocate() noexcept {
        if (!freelist_built_) [[unlikely]]
            init_freelist_();
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
        p->~T();
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

    void init_freelist_() noexcept {
        for (std::size_t i = 0; i + 1 < Capacity; ++i) {
            store_link_(&(*storage_)[i], &(*storage_)[i + 1]);
        }
        store_link_(&(*storage_)[Capacity - 1], nullptr);
        free_head_ = &(*storage_)[0];
        freelist_built_ = true;
    }

    alignas(64) std::unique_ptr<slot_array> storage_;
    slot* free_head_{nullptr};
    std::size_t in_use_{0};
    bool freelist_built_{false};
};

}  // namespace lob

#endif  // LOB_ARENA_HPP
