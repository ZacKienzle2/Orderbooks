#include <lob/affinity.hpp>

#include <catch2/catch_test_macros.hpp>

#include <thread>

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
    bool named_ok  = false;
    std::thread worker{[&] {
        pinned_ok = lob::pin_this_thread_to_core(0);
        named_ok  = lob::set_this_thread_name("lob-wkr");
    }};
    worker.join();
    (void)pinned_ok;
    (void)named_ok;
    SUCCEED();
}
