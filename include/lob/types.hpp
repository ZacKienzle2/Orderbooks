#ifndef LOB_TYPES_HPP
#define LOB_TYPES_HPP

#include <cstdint>
#include <type_traits>

namespace lob {

using tick_t       = std::uint32_t;
using qty_t        = std::uint64_t;
using order_id_t   = std::uint64_t;
using seq_t        = std::uint64_t;
using account_id_t = std::uint32_t;

enum class side : std::uint8_t { bid = 0, ask = 1 };
enum class tif  : std::uint8_t { gtc = 0, ioc = 1, fok = 2 };

// Branchless flip of bid <-> ask. Defined for `side::bid` and `side::ask`;
// any other underlying value is a contract violation (XOR keeps the result
// representable but the value is meaningless).
[[nodiscard]] constexpr side opposite(side s) noexcept {
    return static_cast<side>(static_cast<std::underlying_type_t<side>>(s) ^ 1U);
}

}  // namespace lob

#endif  // LOB_TYPES_HPP
