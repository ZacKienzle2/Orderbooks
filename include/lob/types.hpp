#ifndef LOB_TYPES_HPP
#define LOB_TYPES_HPP

#include <cstdint>
#include <type_traits>

namespace lob {

using tick_t     = std::uint32_t;
using qty_t      = std::uint64_t;
using order_id_t = std::uint64_t;
using seq_t      = std::uint64_t;

enum class side : std::uint8_t { bid = 0, ask = 1 };
enum class tif  : std::uint8_t { gtc = 0, ioc = 1, fok = 2 };

[[nodiscard]] constexpr side opposite(side s) noexcept {
    return static_cast<side>(1U - static_cast<std::underlying_type_t<side>>(s));
}

}  // namespace lob

#endif  // LOB_TYPES_HPP
