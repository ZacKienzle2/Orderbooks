#ifndef LOB_CONFIG_HPP
#define LOB_CONFIG_HPP

#include <lob/types.hpp>

#include <cstdint>

namespace lob {

enum class self_cross_policy : std::uint8_t {
    cancel_newest = 0,
    cancel_oldest = 1,
    decrement_trade = 2,
};

struct engine_config {
    // Smallest representable price increment. Engine prices are integer
    // multiples of tick_size. The dense ladder spans [0, Ticks) ticks.
    tick_t tick_size{1};

    // Hard cap on a single order's quantity. The gateway validator enforces
    // it on submit and modify, and restore() rejects snapshot records above
    // it. The cap also bounds level aggregates. With MaxOrders orders of at
    // most this quantity, a qty_t sum stays far below overflow (2^16 orders
    // at the default cap reach 2^48).
    qty_t max_order_qty{1ULL << 32};

    // Behaviour when an incoming aggressor would match against a resting
    // order from the same account. See ADR-0012.
    self_cross_policy self_cross{self_cross_policy::cancel_newest};

    // When true, the engine emits a top-of-book event only when best price
    // or best quantity changes. When false, every state mutation emits one
    // (useful for testing).
    bool top_throttle{true};
};

}  // namespace lob

#endif  // LOB_CONFIG_HPP
