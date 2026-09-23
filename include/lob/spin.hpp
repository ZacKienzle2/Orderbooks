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
// It also paces the poll of a line another core is about to write. With the
// hint replaced by a bare compiler barrier in every spin loop, lob_loadgen's
// unloaded p50 rose from 754 to 1126 reference cycles over ten balanced runs,
// because an unthrottled reader keeps pulling the line the writer needs to
// own. Keep it.
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

// How many cpu_relax hints a bounded spin runs before it gives the core back.
//
// Karlin, Manasse, McGeoch and Owicki (doi:10.1145/121132.286599) showed that
// spinning for exactly the cost of the blocking alternative, then blocking, is
// within a factor of two of the optimal offline strategy whatever the wait
// turns out to be. So the budget is not a preference: it is the cost of the
// alternative, divided by the cost of one hint.
//
// Both sides are measurable. On this development host a cpu_relax costs about
// 18 ns, and parking on an atomic until another thread wakes it costs between
// 5 and 24 us, the spread being what a shared, virtualised host does to a
// futex wake. That puts the competitive budget between about 280 and 1,370
// hints. A worker pinned to a core of its own should sit at the top of that
// range, because the core has nothing else to run.
//
// Re-measure on a host that matters rather than carrying this number to it:
// the two probes are a spin loop against sched_yield, and a two-thread park
// and wake.
inline constexpr unsigned default_spin_budget = 1024;

}  // namespace lob

#endif  // LOB_SPIN_HPP
