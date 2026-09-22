#ifndef LOB_TSC_HPP
#define LOB_TSC_HPP

#include <cstdint>

#if !defined(__x86_64__) && !defined(__i386__)
#include <chrono>
#endif

namespace lob {

// Low-overhead timestamp for latency sampling. On x86 it reads the invariant
// time-stamp counter, whose unit is reference cycles and whose read costs a few
// cycles, so it does not swamp a sub-microsecond operation. Elsewhere it is the
// monotonic clock in nanoseconds. The value is a difference operand only; its
// epoch and unit are host-specific.
[[nodiscard]] inline std::uint64_t read_tsc() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    return __builtin_ia32_rdtsc();
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

}  // namespace lob

#endif  // LOB_TSC_HPP
