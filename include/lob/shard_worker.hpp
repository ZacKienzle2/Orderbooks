#ifndef LOB_SHARD_WORKER_HPP
#define LOB_SHARD_WORKER_HPP

#include <lob/affinity.hpp>
#include <lob/messages.hpp>
#include <lob/spin.hpp>
#include <lob/spsc_ring.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>

namespace lob {

// How far ahead of the command being applied apply_batch runs each of the
// engine's prefetch stages, in commands. Zero turns a stage off.
//
// A command on the resting path costs a short chain of dependent last-level
// cache hits, the index slot and then the order it names, and one command has
// nothing to overlap them with. The lob_profile stream workload drains the deep
// mix through apply_batch a claim at a time. Over 21 interleaved rounds on one
// pinned core, the index stage two commands ahead and the order stage one ahead
// took a 64-command drain from 156 to 125 cycles per op at the minimum and from
// 167 to 132 at the median, and a 16-command drain from 163 to 138 at the
// median. The index stage alone reached 146. A batch of one, which has nothing
// to look ahead to, moved from 210 to 214, inside the noise. Distances of eight
// and more gained less, because the latency being hidden is a last-level hit of
// about fifty cycles, and a third stage that also prefetched the order's level
// and FIFO neighbours lost ground. See ADR-0038.
struct prefetch_plan {
    unsigned index_ahead{2};
    unsigned order_ahead{1};
};

// Host placement, busy-wait and prefetch policy shared by every shard worker.
//
// When pin_threads is set, worker i is pinned to core (first_core + i *
// core_stride) via lob::pin_this_thread_to_core. A stride above one skips
// SMT siblings or interleaves across sockets, depending on the host's core
// enumeration. Pinning is best effort. A platform that ignores the hint, or
// any non-Linux and non-macOS target, leaves the worker unpinned and the
// runtime still functions.
//
// spin_budget bounds the busy-wait. A worker that finds its ingress ring
// empty spins with cpu_relax up to spin_budget times, then yields to the
// scheduler. A large budget minimises wake latency on a dedicated isolated
// core. A small one returns the core to other work sooner on a shared host.
//
// prefetch sets how far ahead of each command the worker prefetches within a
// drained batch. The best distance follows the host's cache latency, so it is
// a setting rather than a constant.
struct shard_runtime_config {
    bool pin_threads{true};
    std::size_t first_core{0};
    std::size_t core_stride{1};
    unsigned spin_budget{1024};
    prefetch_plan prefetch{};
};

// A 64-bit counter padded to its own cache line. A worker's release store to
// its own counter never invalidates a neighbour's line, and a producer that
// polls every counter for drain reads them without false sharing.
struct alignas(64) padded_atomic {
    std::atomic<std::uint64_t> value{0};
};

// Apply one decoded command to a shard's engine and return the handle the
// engine reports, the resting order's for a submit or a modify and an empty
// one for a cancel. The engine owns the matching semantics; this only fans the tagged
// union out to the right entry point. An engine whose entry points return
// nothing yields an empty handle.
template <class Engine>
inline order_handle apply_command(Engine& eng, const command& c) noexcept {
    const auto handle_from = [](auto&& call) noexcept -> order_handle {
        if constexpr (std::is_void_v<decltype(call())>) {
            call();
            return {};
        } else {
            return call();
        }
    };
    switch (c.k) {
        case command::kind::submit:
            return handle_from([&] { return eng.on_submit(c.body.submit); });
        case command::kind::cancel:
            eng.on_cancel(c.body.cancel);
            return {};
        case command::kind::modify:
            return handle_from([&] { return eng.on_modify(c.body.modify); });
    }
    return {};
}

// Apply the n commands at(0) .. at(n - 1) in order, running engine::prefetch
// plan.index_ahead commands ahead and engine::prefetch_order plan.order_ahead
// commands ahead of each, within the batch. The first commands of a batch are
// prefetched as it opens. Semantics are those of applying each in turn with
// apply_command; the prefetches read and never write. done(i, handle) receives
// each command's result, so a caller that answers its clients, a gateway
// acking an order with its handle, drains through the same path.
template <class Engine, class At, class Done>
inline void apply_batch(Engine& eng, unsigned n, At at, prefetch_plan plan, Done done) noexcept {
    for (unsigned i = 0; i < plan.index_ahead && i < n; ++i)
        eng.prefetch(at(i));
    for (unsigned i = 0; i < plan.order_ahead && i < n; ++i)
        eng.prefetch_order(at(i));
    for (unsigned i = 0; i < n; ++i) {
        if (plan.index_ahead > 0 && i + plan.index_ahead < n)
            eng.prefetch(at(i + plan.index_ahead));
        if (plan.order_ahead > 0 && i + plan.order_ahead < n)
            eng.prefetch_order(at(i + plan.order_ahead));
        done(i, apply_command(eng, at(i)));
    }
}

template <class Engine, class At>
inline void apply_batch(Engine& eng, unsigned n, At at, prefetch_plan plan = {}) noexcept {
    apply_batch(eng, n, at, plan, [](unsigned, order_handle) noexcept {});
}

// Drive one shard. The worker pins and names itself, then drains its ingress
// ring into the engine until stop is requested, publishing processed after
// each drained batch so a producer can observe quiescence.
//
// On a stop request the worker drains the ring to empty before returning. The
// acquire load of stop pairs with the controller's release store, so every
// command the producer pushed before requesting stop is visible and applied,
// provided the producer stops feeding before it requests stop. Each processed
// update is a release store, so a drain poll that observes the final count has
// also acquired every engine mutation this worker made.
template <class Engine, std::size_t Capacity>
inline void drive_shard(std::size_t idx,
                        spsc_ring<command, Capacity>& ingress,
                        Engine& eng,
                        std::atomic<std::uint64_t>& processed,
                        const std::atomic<bool>& stop,
                        const shard_runtime_config& cfg) noexcept {
    if (cfg.pin_threads) {
        (void)pin_this_thread_to_core(cfg.first_core + idx * cfg.core_stride);
    }
    char name[16];
    std::snprintf(name, sizeof(name), "lob-shard-%02zu", idx);
    (void)set_this_thread_name(name);

    // The processed counter is read only by a draining producer, never on the
    // matching path, so it is published once per drained batch. This worker is
    // its only writer, so the running total lives in a local and each batch
    // publishes it with a release store, which is a plain store on x86 and
    // stlr on AArch64 where a fetch_add is a locked read-modify-write. The
    // release store orders every engine mutation of the batch before the count
    // a producer's acquire load observes, exactly as the read-modify-write did.
    // Each claim is applied through apply_batch, so the prefetch stages look
    // ahead within whatever the ring delivered.
    constexpr unsigned batch = 64;
    const auto run = [&eng, &cfg](unsigned n, auto at) noexcept {
        apply_batch(eng, n, at, cfg.prefetch);
    };
    std::uint64_t done = processed.load(std::memory_order_relaxed);
    unsigned idle = 0;
    for (;;) {
        const unsigned n = ingress.consume_claim(batch, run);
        if (n > 0) {
            done += n;
            processed.store(done, std::memory_order_release);
            idle = 0;
            continue;
        }
        if (stop.load(std::memory_order_acquire)) {
            unsigned m = 0;
            for (unsigned k = ingress.consume_claim(batch, run); k > 0;
                 k = ingress.consume_claim(batch, run)) {
                m += k;
            }
            if (m > 0) {
                done += m;
                processed.store(done, std::memory_order_release);
            }
            return;
        }
        if (idle < cfg.spin_budget) {
            cpu_relax();
            ++idle;
        } else {
            std::this_thread::yield();
        }
    }
}

}  // namespace lob

#endif  // LOB_SHARD_WORKER_HPP
