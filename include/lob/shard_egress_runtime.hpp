#ifndef LOB_SHARD_EGRESS_RUNTIME_HPP
#define LOB_SHARD_EGRESS_RUNTIME_HPP

#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/hash.hpp>
#include <lob/messages.hpp>
#include <lob/ring_publisher.hpp>
#include <lob/shard_worker.hpp>
#include <lob/spsc_ring.hpp>
#include <lob/types.hpp>

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace lob {

// Threaded multi-symbol runtime with a private egress ring per shard.
//
// Each shard owns an independent engine, a dedicated SPSC ingress ring of
// commands, and a dedicated SPSC egress ring of events. Worker i is the only
// thread that drains ingress ring i and the only producer of egress ring i,
// so both boundaries hold the wait-free single-producer, single-consumer
// contract with no lock anywhere on the path.
//
// This is the difference from shard_runtime. There the workers share one
// publisher and the caller must make it thread safe. Here each engine
// publishes into its own ring_publisher, so no event sink is ever shared
// across threads and the matching path stays contention free. A downstream
// consumer drains each shard's egress ring with try_poll, one consumer per
// shard, fanning events out by routing context with no cross-shard locking.
//
// Lifecycle matches shard_runtime. Construct, optionally warm-start a shard
// via shard(i).restore before start(), call start() once to spawn the workers,
// feed commands through the try_* methods, drain egress with try_poll, then
// stop() to drain ingress and join. drain() without stop() waits for ingress
// quiescence so the controller can read book state mid-run.
template <std::size_t Ticks,
          std::size_t MaxOrders,
          std::size_t NumShards,
          std::size_t IngressCapacity,
          std::size_t EgressCapacity>
class shard_egress_runtime {
    static_assert(NumShards > 0, "shard_egress_runtime: NumShards must be positive");
    static_assert(std::has_single_bit(NumShards),
                  "shard_egress_runtime: NumShards must be a power of two");

  public:
    using publisher_type = ring_publisher<EgressCapacity>;
    using engine_type = engine<publisher_type, Ticks, MaxOrders>;
    using ingress_ring = spsc_ring<command, IngressCapacity>;
    using egress_ring = spsc_ring<event, EgressCapacity>;

    explicit shard_egress_runtime(engine_config cfg, shard_runtime_config rt = {}) : rt_(rt) {
        for (std::size_t i = 0; i < NumShards; ++i) {
            pubs_[i].bind(egress_[i]);
            engines_[i] = std::make_unique<engine_type>(pubs_[i], cfg);
        }
    }

    shard_egress_runtime(const shard_egress_runtime&) = delete;
    shard_egress_runtime(shard_egress_runtime&&) = delete;
    shard_egress_runtime& operator=(const shard_egress_runtime&) = delete;
    shard_egress_runtime& operator=(shard_egress_runtime&&) = delete;

    ~shard_egress_runtime() { stop(); }

    void start() {
        assert(!running_ && "shard_egress_runtime: start() called while already running");
        stop_.store(false, std::memory_order_relaxed);
        for (std::size_t i = 0; i < NumShards; ++i) {
            workers_[i] = std::thread([this, i] {
                drive_shard(i, ingress_[i], *engines_[i], processed_[i].value, stop_, rt_);
            });
        }
        running_ = true;
    }

    void stop() noexcept {
        if (!running_) {
            return;
        }
        stop_.store(true, std::memory_order_release);
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
        running_ = false;
    }

    [[nodiscard]] bool try_submit(symbol_id_t sym, const submit_msg& m) noexcept {
        return enqueue_(sym, command::make_submit(m));
    }

    [[nodiscard]] bool try_cancel(symbol_id_t sym, const cancel_msg& m) noexcept {
        return enqueue_(sym, command::make_cancel(m));
    }

    [[nodiscard]] bool try_modify(symbol_id_t sym, const modify_msg& m) noexcept {
        return enqueue_(sym, command::make_modify(m));
    }

    // Pop one event from a shard's egress ring. Returns false when the ring is
    // empty. Call from a single consumer thread per shard.
    [[nodiscard]] bool try_poll(std::size_t shard_idx, event& out) noexcept {
        return egress_[shard_idx].try_pop(out);
    }

    // Block the producer thread until every command pushed so far has been
    // processed by its worker. See shard_runtime::drain for the ordering
    // argument; the acquire load that observes the final count synchronises
    // with the worker's release store.
    void drain() const noexcept {
        for (std::size_t i = 0; i < NumShards; ++i) {
            while (processed_[i].value.load(std::memory_order_acquire) != pushed_[i]) {
                cpu_relax();
            }
        }
    }

    [[nodiscard]] engine_type& shard(std::size_t idx) noexcept { return *engines_[idx]; }

    [[nodiscard]] const engine_type& shard(std::size_t idx) const noexcept {
        return *engines_[idx];
    }

    [[nodiscard]] const publisher_type& publisher(std::size_t idx) const noexcept {
        return pubs_[idx];
    }

    [[nodiscard]] std::size_t shard_index_for(symbol_id_t sym) const noexcept {
        return shard_index(sym, NumShards);
    }

    [[nodiscard]] static constexpr std::size_t shard_count() noexcept { return NumShards; }

    [[nodiscard]] static constexpr std::size_t ingress_capacity() noexcept {
        return IngressCapacity;
    }

    [[nodiscard]] static constexpr std::size_t egress_capacity() noexcept { return EgressCapacity; }

  private:
    [[nodiscard]] bool enqueue_(symbol_id_t sym, const command& c) noexcept {
        const auto idx = shard_index(sym, NumShards);
        if (!ingress_[idx].try_push(c)) {
            return false;
        }
        ++pushed_[idx];
        return true;
    }

    shard_runtime_config rt_;
    // Declared before the engines so it outlives them; each engine holds a
    // reference to its publisher, which holds a pointer into egress_.
    std::array<egress_ring, NumShards> egress_{};
    std::array<publisher_type, NumShards> pubs_{};
    std::array<std::unique_ptr<engine_type>, NumShards> engines_{};
    std::array<ingress_ring, NumShards> ingress_{};
    std::array<padded_atomic, NumShards> processed_{};
    // Owned solely by the producer thread; never read by the workers.
    std::array<std::uint64_t, NumShards> pushed_{};
    std::array<std::thread, NumShards> workers_{};
    std::atomic<bool> stop_{false};
    // Touched only by the controlling thread across start / stop / destroy.
    bool running_{false};
};

}  // namespace lob

#endif  // LOB_SHARD_EGRESS_RUNTIME_HPP
