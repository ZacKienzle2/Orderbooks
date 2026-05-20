#ifndef LOB_SHARD_ROUTER_HPP
#define LOB_SHARD_ROUTER_HPP

#include <lob/concepts.hpp>
#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>

#ifndef NDEBUG
    #include <cassert>
    #include <thread>
#endif

namespace lob {

// Per-symbol shard router over a fixed number of engine instances.
//
// Each shard owns an independent lob::engine; the router maps an inbound
// symbol_id to a shard via a SplitMix64 hash truncated to log2(NumShards)
// bits. The dispatch step is one mul-shift-xor sequence plus a mask, so
// the hot-path overhead per command is roughly fifteen cycles on a modern
// x86 core - negligible compared to the engine itself.
//
// Threading is deliberately not part of this layer: the router is
// single-threaded and the caller drives shards in whatever order it
// chooses. To pin each shard to its own core, wrap the router's
// shard(i) reference in a thread loop pinned via sched_setaffinity
// (Linux) or thread_policy_set (macOS).
//
// All engines share one publisher reference. If the publisher is itself
// per-shard (one ring per consumer), supply a publisher that fans out to
// the correct ring based on the event's seq stamp or the routing context.
template <publisher P, std::size_t Ticks, std::size_t MaxOrders, std::size_t NumShards>
class shard_router {
    static_assert(NumShards > 0, "shard_router: NumShards must be positive");
    static_assert(std::has_single_bit(NumShards), "shard_router: NumShards must be a power of two");

    static constexpr std::uint64_t mask = NumShards - 1;

  public:
    using engine_type = engine<P, Ticks, MaxOrders>;

    shard_router(P& pub, engine_config cfg) {
        for (std::size_t i = 0; i < NumShards; ++i) {
            engines_[i] = std::make_unique<engine_type>(pub, cfg);
        }
    }

    shard_router(const shard_router&) = delete;
    shard_router(shard_router&&) = delete;
    shard_router& operator=(const shard_router&) = delete;
    shard_router& operator=(shard_router&&) = delete;
    ~shard_router() = default;

    void on_submit(symbol_id_t sym, const submit_msg& m) noexcept {
        check_owner_thread_();
        shard_for(sym).on_submit(m);
    }

    void on_cancel(symbol_id_t sym, const cancel_msg& m) noexcept {
        check_owner_thread_();
        shard_for(sym).on_cancel(m);
    }

    void on_modify(symbol_id_t sym, const modify_msg& m) noexcept {
        check_owner_thread_();
        shard_for(sym).on_modify(m);
    }

    [[nodiscard]] engine_type& shard(std::size_t idx) noexcept { return *engines_[idx]; }

    [[nodiscard]] const engine_type& shard(std::size_t idx) const noexcept {
        return *engines_[idx];
    }

    [[nodiscard]] std::size_t shard_index_for(symbol_id_t sym) const noexcept {
        return splitmix64(sym) & mask;
    }

    [[nodiscard]] static constexpr std::size_t shard_count() noexcept { return NumShards; }

  private:
    [[nodiscard]] engine_type& shard_for(symbol_id_t sym) noexcept {
        return *engines_[shard_index_for(sym)];
    }

    // SplitMix64: cheap full-avalanche hash. Plenty of distribution quality
    // for power-of-two-modulus sharding; one mul + two xor-shifts per call.
    [[nodiscard]] static constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    }

    // Records the thread id of the first dispatch call and asserts every
    // subsequent dispatch comes from the same thread. Debug-only: the
    // router is single-threaded by contract, but nothing else enforces it.
    // No cost in release builds; the member and the call collapse to nothing.
    void check_owner_thread_() noexcept {
#ifndef NDEBUG
        const auto self = std::this_thread::get_id();
        if (owner_ == std::thread::id{}) {
            owner_ = self;
            return;
        }
        assert(owner_ == self && "shard_router accessed from multiple threads");
#endif
    }

    std::array<std::unique_ptr<engine_type>, NumShards> engines_{};
#ifndef NDEBUG
    std::thread::id owner_{};
#endif
};

}  // namespace lob

#endif  // LOB_SHARD_ROUTER_HPP
