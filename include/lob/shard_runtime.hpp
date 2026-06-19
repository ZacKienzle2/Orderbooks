#ifndef LOB_SHARD_RUNTIME_HPP
#define LOB_SHARD_RUNTIME_HPP

#include <lob/affinity.hpp>
#include <lob/concepts.hpp>
#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/shard_router.hpp>
#include <lob/spin.hpp>
#include <lob/spsc_ring.hpp>
#include <lob/types.hpp>

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace lob {

// Host placement and busy-wait policy for the shard worker threads.
//
// When pin_threads is set, worker i is pinned to core (first_core + i *
// core_stride) via lob::pin_this_thread_to_core. A stride above one skips
// SMT siblings or interleaves across sockets, depending on the host's core
// enumeration. Pinning is best effort. A platform that ignores the hint (or
// any non-Linux, non-macOS target) leaves the worker unpinned and the
// runtime still functions.
//
// spin_budget bounds the busy-wait. A worker that finds its ingress ring
// empty spins with cpu_relax up to spin_budget times, then yields to the
// scheduler. A large budget minimises wake latency on a dedicated isolated
// core; a small one returns the core to other work sooner on a shared host.
struct shard_runtime_config {
    bool pin_threads{true};
    std::size_t first_core{0};
    std::size_t core_stride{1};
    unsigned spin_budget{1024};
};

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

    static void dispatch_(engine_type& eng, const command& c) noexcept {
        switch (c.k) {
            case command::kind::submit:
                eng.on_submit(c.body.submit);
                break;
            case command::kind::cancel:
                eng.on_cancel(c.body.cancel);
                break;
            case command::kind::modify:
                eng.on_modify(c.body.modify);
                break;
        }
    }

    void run_shard_(std::size_t idx) noexcept {
        if (rt_.pin_threads) {
            (void)pin_this_thread_to_core(rt_.first_core + idx * rt_.core_stride);
        }
        char name[16];
        std::snprintf(name, sizeof(name), "lob-shard-%02zu", idx);
        (void)set_this_thread_name(name);

        auto& ring = ingress_[idx];
        auto& eng = router_.shard(idx);
        auto& processed = processed_[idx].value;

        command c;
        unsigned idle = 0;
        for (;;) {
            if (ring.try_pop(c)) {
                dispatch_(eng, c);
                processed.fetch_add(1, std::memory_order_release);
                idle = 0;
                continue;
            }
            if (stop_.load(std::memory_order_acquire)) {
                // The producer no longer pushes once stop is observed, and the
                // acquire above makes every prior push visible, so this drains
                // the ring to completion before the worker exits.
                while (ring.try_pop(c)) {
                    dispatch_(eng, c);
                    processed.fetch_add(1, std::memory_order_release);
                }
                return;
            }
            if (idle < rt_.spin_budget) {
                cpu_relax();
                ++idle;
            } else {
                std::this_thread::yield();
            }
        }
    }

    // One processed counter per shard, each on its own cache line so a
    // worker's release store never invalidates a neighbour's line and the
    // producer's drain poll reads them without false sharing.
    struct alignas(64) padded_counter {
        std::atomic<std::uint64_t> value{0};
    };

    router_type router_;
    shard_runtime_config rt_;
    std::array<ring_type, NumShards> ingress_{};
    std::array<padded_counter, NumShards> processed_{};
    // Owned solely by the producer thread; never read by the workers.
    std::array<std::uint64_t, NumShards> pushed_{};
    std::array<std::thread, NumShards> workers_{};
    std::atomic<bool> stop_{false};
    // Touched only by the controlling thread across start / stop / destroy.
    bool running_{false};
};

}  // namespace lob

#endif  // LOB_SHARD_RUNTIME_HPP
