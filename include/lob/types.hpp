#ifndef LOB_TYPES_HPP
#define LOB_TYPES_HPP

#include <cstdint>
#include <type_traits>

namespace lob {

using tick_t = std::uint32_t;
using qty_t = std::uint64_t;
using order_id_t = std::uint64_t;
using seq_t = std::uint64_t;
using account_id_t = std::uint32_t;
using symbol_id_t = std::uint64_t;

// How a cancel or modify reaches the order it names. by_id looks the id up in
// an index the engine keeps. by_handle carries the handle the engine returned
// when the order rested, and the engine keeps no index (ADR-0042).
enum class order_lookup : std::uint8_t { by_id = 0, by_handle = 1 };

// Names a resting order by where the engine keeps it, its arena slot and that
// slot's generation, which is odd while the slot is allocated. The value-
// initialised handle names nothing.
struct order_handle {
    std::uint32_t slot{0};
    std::uint32_t generation{0};

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return generation != 0; }

    friend constexpr bool operator==(order_handle, order_handle) noexcept = default;
};

enum class side : std::uint8_t { bid = 0, ask = 1 };
enum class tif : std::uint8_t { gtc = 0, ioc = 1, fok = 2 };

// Branchless flip of bid <-> ask. Defined for `side::bid` and `side::ask`;
// any other underlying value is a contract violation (XOR keeps the result
// representable but the value is meaningless).
[[nodiscard]] constexpr side opposite(side s) noexcept {
    return static_cast<side>(static_cast<std::underlying_type_t<side>>(s) ^ 1U);
}

}  // namespace lob

#endif  // LOB_TYPES_HPP
