#ifndef LOB_SPSC_RING_HPP
#define LOB_SPSC_RING_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lob {

// Wait-free bounded single-producer / single-consumer ring buffer.
//
// Each slot carries the sequence number that publishes it, in the same cache
// line as its payload. The producer writes the payload, then stores the slot's
// sequence with release; the consumer polls the slot it expects next and reads
// the payload once the sequence matches. The consumer never reads the
// producer's cursor, so a handoff moves one cache line from the producer's
// core to the consumer's, where polling a shared head index moved two, the
// index line and then the slot line, one after the other. This is the
// decoupling FastForward (Giacomoni, Moseley and Vachharajani,
// doi:10.1145/1345206.1345215) applies to pipeline queues, with a sequence in
// place of a null sentinel so any trivially copyable T can travel.
//
// A slot is sized to a power of two up to a cache line and to whole lines past
// one, so no slot straddles two lines. The consumer's cursor still gates the
// producer against overrun, read through a producer-side cache as before, and
// the producer still maintains its own cursor for size() and the observers.
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

    // Payload and publishing sequence share a slot. seq holds pos + 1 once the
    // producer has written position pos into the slot, and grows by Capacity
    // on every lap, so a stale value from an earlier lap never matches.
    static constexpr std::size_t slot_align =
        std::min<std::size_t>(64, std::bit_ceil(sizeof(T) + sizeof(std::uint64_t)));

    struct alignas(slot_align) slot {
        T value{};
        std::atomic<std::uint64_t> seq{0};
    };

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
        slot& s = buf_[head & mask];
        s.value = value;
        s.seq.store(head + 1, std::memory_order_release);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        const slot& s = buf_[tail & mask];
        if (s.seq.load(std::memory_order_acquire) != tail + 1) [[unlikely]]
            return false;
        out = s.value;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consume up to max_n queued elements in one claim, invoking fn with a
    // const reference to each slot in FIFO order, and return the count
    // consumed. The batch pays one release store of the consumer cursor for
    // the whole run, where a try_pop loop pays one per element. fn receives
    // the slot in place, so no element is copied out of the ring. The slots
    // stay claimed until the trailing release store, so fn must finish
    // reading each element before returning; it must not re-enter this ring.
    // fn is invoked under the same single-consumer contract as try_pop and
    // must be noexcept in effect.
    template <class F>
    [[nodiscard]] unsigned consume_batch(unsigned max_n, F fn) noexcept {
        return consume_claim(max_n, [&fn](unsigned n, auto at) noexcept {
            for (unsigned i = 0; i < n; ++i)
                fn(at(i));
        });
    }

    // The claim consume_batch walks, handed over whole. fn receives the count
    // claimed and an accessor, at(i) naming the i-th slot in FIFO order, so a
    // consumer can read ahead of the element it is processing, which a
    // one-element callback cannot. The same claim and release rules hold.
    //
    // The claim counts the published slots from the consumer cursor. Their
    // sequence loads are independent of one another, so the core issues them
    // together and the scan brings the claimed lines in side by side.
    template <class F>
    [[nodiscard]] unsigned consume_claim(unsigned max_n, F fn) noexcept {
        const auto tail = tail_.load(std::memory_order_relaxed);
        unsigned n = 0;
        while (n < max_n &&
               buf_[(tail + n) & mask].seq.load(std::memory_order_acquire) == tail + n + 1)
            ++n;
        if (n == 0) [[unlikely]]
            return 0;
        fn(n,
           [this, tail](unsigned i) noexcept -> const T& { return buf_[(tail + i) & mask].value; });
        tail_.store(tail + n, std::memory_order_release);
        return n;
    }

    // Best-effort observers; valid only for owner-side or external
    // synchronisation. Two separate acquire loads of head_ and tail_ are
    // not an atomic snapshot, so a concurrent producer or consumer can
    // observe a partially-updated pair; the unsigned subtraction
    // h - t can transiently appear inverted near the boundary. Use
    // these for diagnostics, never as the gate on a hot-path decision.
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
    // cache check is a same-line read; tail_ sits on the consumer's line.
    // Lines are 64-byte aligned to prevent false sharing between producer
    // and consumer cores. buf_ lives on its own lines so slot writes never
    // invalidate the cursor lines.
    //
    // tail_cache_ is owned by the producer (read and written only inside
    // try_push). It is not atomic because no cross-side access is
    // permitted. A non-owner observer that touched it would see
    // arbitrarily stale data and could misclassify the ring's fullness.
    // Do not add such an observer.
    alignas(64) std::atomic<std::uint64_t> head_{0};
    std::uint64_t tail_cache_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    alignas(64) std::array<slot, Capacity> buf_{};
};

}  // namespace lob

#endif  // LOB_SPSC_RING_HPP
