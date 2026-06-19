#ifndef LOB_EGRESS_MERGER_HPP
#define LOB_EGRESS_MERGER_HPP

#include <lob/affinity.hpp>
#include <lob/messages.hpp>
#include <lob/spin.hpp>

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace lob {

// A source of per-shard egress events. shard_egress_runtime is the canonical
// model: try_poll pops one event from a shard's ring, and shard_count reports
// how many shards there are.
template <class S>
concept multi_egress_source = requires(S s, event& e) {
    { s.try_poll(std::size_t{0}, e) } noexcept -> std::same_as<bool>;
    { S::shard_count() } -> std::convertible_to<std::size_t>;
};

// A downstream that receives every merged event in stream order. The second
// argument is the global merge sequence, a gap-free counter assigned in the
// order the merger forwards events, so the sink sees one totally ordered
// stream across all shards.
template <class K>
concept merge_sink = requires(K k, const event& e, std::uint64_t seq) {
    { k.on_event(e, seq) } noexcept;
};

// Host placement and busy-wait policy for the single merger thread.
struct merger_config {
    bool pin_thread{true};
    std::size_t core{0};
    unsigned spin_budget{1024};
};

// Single-threaded fan-in over the per-shard egress rings of a runtime.
//
// The per-shard egress design (ADR-0020) leaves each shard with its own event
// ring, which is ideal for the matching threads but inconvenient for a
// downstream that wants one feed. The merger is the single consumer of every
// shard's egress ring, so it preserves each ring's single-producer,
// single-consumer contract, and it forwards events to one sink stamped with a
// gap-free global sequence. The result is a single ordered stream that a
// recorder or wire publisher can consume without touching per-shard state.
//
// The merger owns the consumer side of the egress rings while it runs, so the
// controller must not also call the source's try_poll during that window. To
// shut down cleanly, quiesce the producing runtime first (drain then stop, or
// stop), then stop() the merger, which makes a final pass over every ring
// before its thread exits so no already-published event is left behind.
template <multi_egress_source Source, merge_sink Sink>
class egress_merger {
  public:
    egress_merger(Source& src, Sink& sink, merger_config cfg = {})
        : src_(&src), sink_(&sink), cfg_(cfg) {}

    egress_merger(const egress_merger&) = delete;
    egress_merger(egress_merger&&) = delete;
    egress_merger& operator=(const egress_merger&) = delete;
    egress_merger& operator=(egress_merger&&) = delete;

    ~egress_merger() { stop(); }

    void start() {
        stop_.store(false, std::memory_order_relaxed);
        worker_ = std::thread([this] { run_(); });
    }

    void stop() noexcept {
        if (!worker_.joinable()) {
            return;
        }
        stop_.store(true, std::memory_order_release);
        worker_.join();
    }

    // Total events forwarded so far. Read it after stop() for a final count,
    // or any time for a relaxed running tally.
    [[nodiscard]] std::uint64_t merged() const noexcept {
        return merged_.load(std::memory_order_relaxed);
    }

  private:
    void run_() noexcept {
        if (cfg_.pin_thread) {
            (void)pin_this_thread_to_core(cfg_.core);
        }
        (void)set_this_thread_name("lob-merger");

        event e;
        unsigned idle = 0;
        for (;;) {
            if (drain_round_(e)) {
                idle = 0;
                continue;
            }
            if (stop_.load(std::memory_order_acquire)) {
                // Producers quiesced before stop, so the acquire makes every
                // published event visible; drain each ring to empty and exit.
                while (drain_round_(e)) {
                }
                return;
            }
            if (idle < cfg_.spin_budget) {
                cpu_relax();
                ++idle;
            } else {
                std::this_thread::yield();
            }
        }
    }

    bool drain_round_(event& e) noexcept {
        bool any = false;
        for (std::size_t s = 0; s < Source::shard_count(); ++s) {
            while (src_->try_poll(s, e)) {
                sink_->on_event(e, seq_);
                ++seq_;
                merged_.store(seq_, std::memory_order_relaxed);
                any = true;
            }
        }
        return any;
    }

    Source* src_;
    Sink* sink_;
    merger_config cfg_;
    // Owned solely by the merger thread.
    std::uint64_t seq_{0};
    std::atomic<std::uint64_t> merged_{0};
    std::atomic<bool> stop_{false};
    std::thread worker_{};
};

}  // namespace lob

#endif  // LOB_EGRESS_MERGER_HPP
