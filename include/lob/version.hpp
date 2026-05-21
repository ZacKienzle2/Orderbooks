#ifndef LOB_VERSION_HPP
#define LOB_VERSION_HPP

#include <cstdint>
#include <string_view>

namespace lob::version {

[[nodiscard]] std::uint32_t major() noexcept;
[[nodiscard]] std::uint32_t minor() noexcept;
[[nodiscard]] std::uint32_t patch() noexcept;
[[nodiscard]] std::string_view string() noexcept;
[[nodiscard]] std::string_view git_sha() noexcept;

}  // namespace lob::version

#endif  // LOB_VERSION_HPP
