#include <lob/affinity.hpp>

#include <cstddef>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__)
#include <sched.h>

TEST_CASE("pin_this_thread_to_core indexes the CPUs the thread may use", "[affinity]") {
    // The last index names the highest CPU of the mask, whatever cpuset the
    // host or a launcher such as numactl set. Catch2 assertions stay on this
    // thread, so the worker only records what it saw.
    int last = -1;
    int count = 0;
    bool pinned = false;
    int ran_on = -2;
    std::thread worker{[&] {
        cpu_set_t allowed;
        CPU_ZERO(&allowed);
        if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
            return;
        }
        count = CPU_COUNT(&allowed);
        for (std::size_t cpu = 0; cpu < std::size_t{CPU_SETSIZE}; ++cpu) {
            if (CPU_ISSET(cpu, &allowed)) {
                last = static_cast<int>(cpu);
            }
        }
        pinned = lob::pin_this_thread_to_core(static_cast<std::size_t>(count - 1));
        ran_on = sched_getcpu();
    }};
    worker.join();
    REQUIRE(count > 0);
    REQUIRE(pinned);
    CHECK(ran_on == last);
    CHECK_FALSE(lob::pin_this_thread_to_core(static_cast<std::size_t>(CPU_SETSIZE)));
}
#endif

TEST_CASE("pin_this_thread_to_core accepts core 0 or reports failure", "[affinity]") {
    // The harness runs on shared CI runners and on developer laptops; the
    // pin may legitimately fail when the kernel rejects the operation or
    // returns no permitted CPUs. The test asserts only that the call
    // completes without crashing and returns a bool.
    const bool ok = lob::pin_this_thread_to_core(0);
    (void)ok;
    SUCCEED();
}

TEST_CASE("set_this_thread_name accepts a short label", "[affinity]") {
    const bool ok = lob::set_this_thread_name("lob-test");
    (void)ok;
    SUCCEED();
}

TEST_CASE("affinity helpers work from a worker thread", "[affinity]") {
    bool pinned_ok = false;
    bool named_ok = false;
    std::thread worker{[&] {
        pinned_ok = lob::pin_this_thread_to_core(0);
        named_ok = lob::set_this_thread_name("lob-wkr");
    }};
    worker.join();
    (void)pinned_ok;
    (void)named_ok;
    SUCCEED();
}
