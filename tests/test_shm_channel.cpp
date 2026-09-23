#include <lob/shm_channel.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using lob::shm_channel;
using lob::shm_region;

namespace {

struct payload {
    std::uint64_t id;
    std::uint64_t value;
};

using channel = shm_channel<payload, payload, 64>;

// Each test names its own segment so a parallel or repeated run never attaches
// to another test's channel, and the owner unlinks the name on destruction.
std::string unique_name(const char* tag) {
    static std::atomic<int> counter{0};
    return std::string{"/lob-test-"} + tag + "-" +
           std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
}

}  // namespace

TEST_CASE("shm_region rejects names POSIX does not allow", "[shm]") {
    const std::size_t bytes = lob::shm_channel_bytes<channel>();
    REQUIRE_FALSE(shm_region::create("lob-no-slash", bytes).valid());
    REQUIRE_FALSE(shm_region::create("/lob/nested", bytes).valid());
    REQUIRE_FALSE(shm_region::create("/", bytes).valid());
    REQUIRE_FALSE(shm_region::create("/lob-zero", 0).valid());
}

TEST_CASE("shm_region attach fails when no owner created the segment", "[shm]") {
    const shm_region r =
        shm_region::attach(unique_name("absent"), lob::shm_channel_bytes<channel>());
    REQUIRE_FALSE(r.valid());
    REQUIRE(r.error() != 0);
}

TEST_CASE("shm_channel is published to a second mapping of the same segment", "[shm]") {
    const std::string name = unique_name("publish");
    const std::size_t bytes = lob::shm_channel_bytes<channel>();

    const shm_region owner = shm_region::create(name, bytes);
    REQUIRE(owner.valid());
    auto* server = lob::shm_channel_init<channel>(owner);
    REQUIRE(server != nullptr);

    const shm_region peer = shm_region::attach(name, bytes);
    REQUIRE(peer.valid());
    auto* client = lob::shm_channel_view<channel>(peer);
    REQUIRE(client != nullptr);
    REQUIRE(lob::shm_channel_wait(client, 1'000'000));

    // Two mappings, one segment: what the client pushes is what the server pops.
    REQUIRE(client->requests.try_push(payload{.id = 7, .value = 11}));
    payload got{};
    REQUIRE(server->requests.try_pop(got));
    REQUIRE(got.id == 7);
    REQUIRE(got.value == 11);

    REQUIRE(server->replies.try_push(payload{.id = 7, .value = 12}));
    REQUIRE(client->replies.try_pop(got));
    REQUIRE(got.value == 12);
}

TEST_CASE("shm_channel carries a request and reply stream across mappings", "[shm]") {
    const std::string name = unique_name("stream");
    const std::size_t bytes = lob::shm_channel_bytes<channel>();

    const shm_region owner = shm_region::create(name, bytes);
    REQUIRE(owner.valid());
    auto* server = lob::shm_channel_init<channel>(owner);
    REQUIRE(server != nullptr);

    const shm_region peer = shm_region::attach(name, bytes);
    REQUIRE(peer.valid());
    auto* client = lob::shm_channel_view<channel>(peer);
    REQUIRE(client != nullptr);
    REQUIRE(lob::shm_channel_wait(client, 1'000'000));

    constexpr std::uint64_t n = 10'000;

    // The server echoes each request with its value doubled, so a reply that
    // matched by luck rather than by transport would not survive the check.
    std::thread serving([server] {
        for (std::uint64_t seen = 0; seen < n;) {
            payload req{};
            if (!server->requests.try_pop(req)) {
                lob::cpu_relax();
                continue;
            }
            while (!server->replies.try_push(payload{.id = req.id, .value = req.value * 2}))
                lob::cpu_relax();
            ++seen;
        }
    });

    std::uint64_t replies = 0;
    for (std::uint64_t i = 0; i < n; ++i) {
        while (!client->requests.try_push(payload{.id = i, .value = i}))
            lob::cpu_relax();
        payload rsp{};
        while (!client->replies.try_pop(rsp))
            lob::cpu_relax();
        REQUIRE(rsp.id == i);
        REQUIRE(rsp.value == i * 2);
        ++replies;
    }
    serving.join();
    REQUIRE(replies == n);
}

TEST_CASE("shm_channel_wait gives up rather than spinning forever", "[shm]") {
    const std::string name = unique_name("unready");
    const std::size_t bytes = lob::shm_channel_bytes<channel>();

    // A segment sized but never initialised: the ready word stays zero, so a
    // client attaching to a server that died before publishing backs out.
    const shm_region owner = shm_region::create(name, bytes);
    REQUIRE(owner.valid());
    const shm_region peer = shm_region::attach(name, bytes);
    REQUIRE(peer.valid());
    auto* client = lob::shm_channel_view<channel>(peer);
    REQUIRE(client != nullptr);
    REQUIRE_FALSE(lob::shm_channel_wait(client, 1000));
}

TEST_CASE("shm_region unlinks the segment it owns", "[shm]") {
    const std::string name = unique_name("unlink");
    const std::size_t bytes = lob::shm_channel_bytes<channel>();
    {
        const shm_region owner = shm_region::create(name, bytes);
        REQUIRE(owner.valid());
        REQUIRE(shm_region::attach(name, bytes).valid());
    }
    // The owner is gone, so the name is too and a late client cannot attach to
    // a stale segment holding a dead server's rings.
    REQUIRE_FALSE(shm_region::attach(name, bytes).valid());
}
