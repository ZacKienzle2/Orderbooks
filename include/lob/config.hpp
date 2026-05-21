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

    // Hard cap on a single submit_msg::qty; used by the FOK precheck and the
    // gateway-side validator.
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
