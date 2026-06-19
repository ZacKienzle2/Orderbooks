#ifndef LOB_RING_PUBLISHER_HPP
#define LOB_RING_PUBLISHER_HPP

#include <lob/messages.hpp>
#include <lob/spsc_ring.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lob {

// Publisher that serialises engine events into a bounded SPSC egress ring as
// the event tagged union. It satisfies the publisher concept, so an engine can
// publish straight into the ring with no intermediate sink.
//
// One engine is the sole producer of its ring and one downstream consumer
// drains it, so the egress path holds the wait-free single-producer,
// single-consumer contract with no lock and no thread-safe shared sink. That
// is the point of giving each shard its own publisher rather than fanning
// every worker into one contended object.
//
// A full ring drops the event and bumps a loss counter rather than blocking
// the matching thread. A downstream that must not lose events sizes the ring
// to its worst-case burst and drains it promptly. The counter is atomic so a
// monitor thread can read it without racing the producer's relaxed bump.
template <std::size_t Capacity>
class ring_publisher {
  public:
    using ring_type = spsc_ring<event, Capacity>;

    ring_publisher() noexcept = default;

    explicit ring_publisher(ring_type& egress) noexcept : egress_(&egress) {}

    // Point the publisher at its egress ring. Used when the owner default
    // constructs an array of publishers and binds each to its ring before any
    // event can be published.
    void bind(ring_type& egress) noexcept { egress_ = &egress; }

    void publish(const fill_msg& m) noexcept { push_(event::make_fill(m)); }

    void publish(const top_msg& m) noexcept { push_(event::make_top(m)); }

    void publish(const trade_msg& m) noexcept { push_(event::make_trade(m)); }

    void publish(const self_trade_msg& m) noexcept { push_(event::make_self_trade(m)); }

    // Number of events dropped because the ring was full at publish time.
    [[nodiscard]] std::uint64_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    void push_(const event& e) noexcept {
        if (!egress_->try_push(e)) [[unlikely]] {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    ring_type* egress_{nullptr};
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace lob

#endif  // LOB_RING_PUBLISHER_HPP
