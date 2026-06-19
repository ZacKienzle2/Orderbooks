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

namespace lob {

// Host placement and busy-wait policy shared by every shard worker.
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
struct shard_runtime_config {
    bool pin_threads{true};
    std::size_t first_core{0};
    std::size_t core_stride{1};
    unsigned spin_budget{1024};
};

// A 64-bit counter padded to its own cache line. A worker's release store to
// its own counter never invalidates a neighbour's line, and a producer that
// polls every counter for drain reads them without false sharing.
struct alignas(64) padded_atomic {
    std::atomic<std::uint64_t> value{0};
};

// Apply one decoded command to a shard's engine. The engine owns the matching
// semantics; this only fans the tagged union out to the right entry point.
template <class Engine>
inline void apply_command(Engine& eng, const command& c) noexcept {
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

// Drive one shard. The worker pins and names itself, then drains its ingress
// ring into the engine until stop is requested, incrementing processed after
// each command so a producer can observe quiescence.
//
// On a stop request the worker drains the ring to empty before returning. The
// acquire load of stop pairs with the controller's release store, so every
// command the producer pushed before requesting stop is visible and applied,
// provided the producer stops feeding before it requests stop. Each processed
// increment is a release store, so a drain poll that observes the final count
// has also acquired every engine mutation this worker made.
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

    command c;
    unsigned idle = 0;
    for (;;) {
        if (ingress.try_pop(c)) {
            apply_command(eng, c);
            processed.fetch_add(1, std::memory_order_release);
            idle = 0;
            continue;
        }
        if (stop.load(std::memory_order_acquire)) {
            while (ingress.try_pop(c)) {
                apply_command(eng, c);
                processed.fetch_add(1, std::memory_order_release);
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
