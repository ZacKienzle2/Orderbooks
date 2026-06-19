#ifndef LOB_SHARD_RUNTIME_HPP
#define LOB_SHARD_RUNTIME_HPP

#include <lob/concepts.hpp>
#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/shard_router.hpp>
#include <lob/shard_worker.hpp>
#include <lob/spsc_ring.hpp>
#include <lob/types.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace lob {

// Threaded executor over a shard_router. Each shard runs on its own worker
// thread, pinned to its own core, draining a dedicated single-producer /
// single-consumer ingress ring of commands into that shard's engine.
//
// The producer side is a single gateway thread that calls try_submit /
// try_cancel / try_modify. Each call hashes the symbol to a shard with the
// router's SplitMix64 mapping and pushes the command onto that shard's ring.
// Because exactly one producer feeds each ring and exactly one worker drains
// it, the SPSC contract holds per shard without a lock anywhere on the path.
// Multi-gateway ingress is a separate concern that an MPSC ring would serve;
// see ADR-0008.
//
// Engine isolation makes the parallelism sound. Worker i is the only thread
// that touches engine i, so no engine field is ever shared across threads
// and price-time priority within a shard is preserved exactly as the
// single-threaded engine guarantees it. The one piece of shared state is the
// publisher P, which every worker calls concurrently. Supply a publisher
// whose publish overloads are thread safe, or give each shard its own egress
// ring and fan out by routing context.
//
// Lifecycle is controlled from one thread. Construct, optionally warm-start
// shards via shard(i).restore before start(), call start() once to spawn the
// workers, feed commands, then stop() to drain and join. drain() without
// stop() waits for quiescence so the controller can read book state mid-run.
template <publisher P,
          std::size_t Ticks,
          std::size_t MaxOrders,
          std::size_t NumShards,
          std::size_t RingCapacity>
class shard_runtime {
  public:
    using router_type = shard_router<P, Ticks, MaxOrders, NumShards>;
    using engine_type = typename router_type::engine_type;
    using ring_type = spsc_ring<command, RingCapacity>;

    shard_runtime(P& pub, engine_config cfg, shard_runtime_config rt = {})
        : router_(pub, cfg), rt_(rt) {}

    shard_runtime(const shard_runtime&) = delete;
    shard_runtime(shard_runtime&&) = delete;
    shard_runtime& operator=(const shard_runtime&) = delete;
    shard_runtime& operator=(shard_runtime&&) = delete;

    // Joins the workers if the caller forgot to. stop() is idempotent and
    // noexcept, so destruction never leaks a thread and never throws.
    ~shard_runtime() { stop(); }

    // Spawn one worker per shard. Must be called from the controlling thread
    // and only while stopped; warm-start restores belong before this call.
    void start() {
        assert(!running_ && "shard_runtime: start() called while already running");
        stop_.store(false, std::memory_order_relaxed);
        for (std::size_t i = 0; i < NumShards; ++i) {
            workers_[i] = std::thread([this, i] { run_shard_(i); });
        }
        running_ = true;
    }

    // Request shutdown and join every worker. Each worker drains the commands
    // its producer pushed before the stop request, so no submitted command is
    // dropped, provided the producer stops feeding before calling stop().
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

    // Push a submit onto the owning shard's ring. Returns false when the ring
    // is full; the caller owns the backpressure decision (retry, drop, or
    // route elsewhere). Call only from the single producer thread.
    [[nodiscard]] bool try_submit(symbol_id_t sym, const submit_msg& m) noexcept {
        return enqueue_(sym, command::make_submit(m));
    }

    [[nodiscard]] bool try_cancel(symbol_id_t sym, const cancel_msg& m) noexcept {
        return enqueue_(sym, command::make_cancel(m));
    }

    [[nodiscard]] bool try_modify(symbol_id_t sym, const modify_msg& m) noexcept {
        return enqueue_(sym, command::make_modify(m));
    }

    // Block the producer thread until every command pushed so far has been
    // processed by its worker. After this returns the engines are quiescent
    // with respect to the producer, so the controller may read book state of
    // any shard safely. The acquire load that observes the final processed
    // count synchronises with the worker's release store, so all of that
    // worker's engine mutations are visible on return.
    void drain() const noexcept {
        for (std::size_t i = 0; i < NumShards; ++i) {
            while (processed_[i].value.load(std::memory_order_acquire) != pushed_[i]) {
                cpu_relax();
            }
        }
    }

    [[nodiscard]] router_type& router() noexcept { return router_; }

    [[nodiscard]] const router_type& router() const noexcept { return router_; }

    [[nodiscard]] engine_type& shard(std::size_t idx) noexcept { return router_.shard(idx); }

    [[nodiscard]] const engine_type& shard(std::size_t idx) const noexcept {
        return router_.shard(idx);
    }

    [[nodiscard]] std::size_t shard_index_for(symbol_id_t sym) const noexcept {
        return router_.shard_index_for(sym);
    }

    [[nodiscard]] static constexpr std::size_t shard_count() noexcept { return NumShards; }

    [[nodiscard]] static constexpr std::size_t ring_capacity() noexcept { return RingCapacity; }

  private:
    [[nodiscard]] bool enqueue_(symbol_id_t sym, const command& c) noexcept {
        const auto idx = router_.shard_index_for(sym);
        if (!ingress_[idx].try_push(c)) {
            return false;
        }
        ++pushed_[idx];
        return true;
    }

    void run_shard_(std::size_t idx) noexcept {
        drive_shard(idx, ingress_[idx], router_.shard(idx), processed_[idx].value, stop_, rt_);
    }

    router_type router_;
    shard_runtime_config rt_;
    std::array<ring_type, NumShards> ingress_{};
    std::array<padded_atomic, NumShards> processed_{};
    // Owned solely by the producer thread; never read by the workers.
    std::array<std::uint64_t, NumShards> pushed_{};
    std::array<std::thread, NumShards> workers_{};
    std::atomic<bool> stop_{false};
    // Touched only by the controlling thread across start / stop / destroy.
    bool running_{false};
};

}  // namespace lob

#endif  // LOB_SHARD_RUNTIME_HPP
