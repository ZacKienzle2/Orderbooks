#ifndef LOB_AFFINITY_HPP
#define LOB_AFFINITY_HPP

#include <cstddef>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#endif

namespace lob {

// Pin the calling thread to a single CPU core. Returns true on success,
// false on any platform error (including kernels that ignore the hint).
//
// Linux: core indexes the CPUs of the calling thread's affinity mask, and the
// thread is pinned to that CPU. A cpuset from Slurm, taskset or numactl so
// keeps a runtime's threads inside it, and on a host whose sockets interleave
// their CPU numbers the first indices share a socket. A thread without such a
// mask has every online CPU, where the index is the CPU number. A thread pins
// once, since its mask afterwards holds one CPU.
// macOS: thread_affinity_policy_set with affinity tag = core + 1. macOS
// honours the tag as a hint rather than a hard pin; cores with the same
// non-zero tag prefer to run on the same L2 cache. Tag 0 disables.
// Other platforms: returns false.
[[nodiscard]] inline bool pin_this_thread_to_core(std::size_t core) noexcept {
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        return false;
    }
    for (std::size_t cpu = 0; cpu < std::size_t{CPU_SETSIZE}; ++cpu) {
        if (!CPU_ISSET(cpu, &allowed)) {
            continue;
        }
        if (core != 0) {
            --core;
            continue;
        }
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
    }
    return false;
#elif defined(__APPLE__)
    thread_affinity_policy_data_t policy{static_cast<integer_t>(core) + 1};
    const auto result =
        thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_AFFINITY_POLICY,
                          reinterpret_cast<thread_policy_t>(&policy), THREAD_AFFINITY_POLICY_COUNT);
    return result == KERN_SUCCESS;
#else
    (void)core;
    return false;
#endif
}

// Best-effort thread name for diagnostics and perf records. Returns true if
// the kernel accepted the name. Linux limits names to 15 characters plus a
// terminator; macOS limits to 63.
[[nodiscard]] inline bool set_this_thread_name(const char* name) noexcept {
#if defined(__linux__)
    return pthread_setname_np(pthread_self(), name) == 0;
#elif defined(__APPLE__)
    return pthread_setname_np(name) == 0;
#else
    (void)name;
    return false;
#endif
}

}  // namespace lob

#endif  // LOB_AFFINITY_HPP
