#ifndef LOB_SPIN_HPP
#define LOB_SPIN_HPP

#if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__)
    #include <intrin.h>
#endif

namespace lob {

// Emit an architecture-specific spin-loop hint for a busy-wait body.
//
// On x86 this lowers to the PAUSE instruction. PAUSE throttles the
// speculative pipeline so the wait loop stops flooding the memory subsystem
// with reorder-buffer traffic, and it cedes the shared execution ports of
// the physical core to a sibling hyperthread for the duration of the stall.
// On AArch64 it lowers to the YIELD hint with the same intent. On any other
// target it degrades to a compiler barrier, which still stops the loop from
// being hoisted, fused, or optimised into a tight read with no observable
// progress.
//
// The hint is advisory. It changes scheduling and power behaviour, never
// program semantics. Use it as the body of a bounded spin before falling
// back to a kernel yield, never as a substitute for one on an unbounded wait.
inline void cpu_relax() noexcept {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #if defined(_MSC_VER) && !defined(__clang__) && !defined(__GNUC__)
    _mm_pause();
    #else
    __builtin_ia32_pause();
    #endif
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

}  // namespace lob

#endif  // LOB_SPIN_HPP
