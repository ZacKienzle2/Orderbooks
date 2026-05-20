#ifndef LOB_SPSC_RING_HPP
#define LOB_SPSC_RING_HPP

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lob {

// Wait-free bounded single-producer / single-consumer ring buffer.
//
// Producer and consumer use independent cursors; each is the sole writer of
// its cursor and the sole reader of the other side's cursor. Cursors are 64-
// byte aligned so producer and consumer never share a cache line. The
// backing array is also 64-byte aligned to keep slot writes off the cursor
// lines.
//
// try_push() and try_pop() are noexcept, branch only on the empty / full
// guard, and never block, spin, or allocate. T must be trivially copyable so
// the slot move is a single memcpy-equivalent assignment without invoking
// user code under the memory fence.
//
// Capacity must be a power of two so the index wrap is a bitmask.
template <class T, std::size_t Capacity>
class spsc_ring {
    static_assert(Capacity > 0, "spsc_ring: Capacity must be positive");
    static_assert(std::has_single_bit(Capacity), "spsc_ring: Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "spsc_ring: T must be trivially copyable");

    static constexpr std::uint64_t mask = Capacity - 1;

  public:
    spsc_ring() noexcept = default;
    ~spsc_ring() = default;
    spsc_ring(const spsc_ring&) = delete;
    spsc_ring(spsc_ring&&) = delete;
    spsc_ring& operator=(const spsc_ring&) = delete;
    spsc_ring& operator=(spsc_ring&&) = delete;

    [[nodiscard]] bool try_push(const T& value) noexcept {
        // LMAX cache trick: the producer caches its last-seen value of
        // the consumer's tail. The common case is "there is room" and the
        // cache satisfies the predicate without crossing the cache line
        // owned by the consumer's core. Only when the cache says full do
        // we pay the acquire load of the remote cursor.
        const auto head = head_.load(std::memory_order_relaxed);
        if (head - tail_cache_ >= Capacity) [[unlikely]] {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head - tail_cache_ >= Capacity) [[unlikely]]
                return false;
        }
        buf_[head & mask] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (head_cache_ == tail) [[unlikely]] {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (head_cache_ == tail) [[unlikely]]
                return false;
        }
        out = buf_[tail & mask];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const auto h = head_.load(std::memory_order_acquire);
        const auto t = tail_.load(std::memory_order_acquire);
        return h - t;  // std::uint64_t -> std::size_t is a no-op on 64-bit targets
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] bool full() const noexcept { return size() == Capacity; }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

  private:
    // Layout: head_ + tail_cache_ share the producer's cache line so the
    // cache check is a same-line read; tail_ + head_cache_ share the
    // consumer's cache line. Lines are 64-byte aligned to prevent false
    // sharing between producer and consumer cores. buf_ lives on its
    // own line so slot writes never invalidate the cursor lines.
    alignas(64) std::atomic<std::uint64_t> head_{0};
    std::uint64_t tail_cache_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    std::uint64_t head_cache_{0};
    alignas(64) std::array<T, Capacity> buf_{};
};

}  // namespace lob

#endif  // LOB_SPSC_RING_HPP
