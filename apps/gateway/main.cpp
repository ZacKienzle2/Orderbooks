// lob_gateway
// -----------
// Binary order-entry gateway over TCP. A client sends fixed-size wire_order
// records; the gateway decodes each into an engine command, runs it, and writes
// back one wire_ack summarising the result. The protocol is one ack per order,
// so a client can measure round-trip latency by timing the ack.
//
// The default mode is a self-test: the process listens on an ephemeral loopback
// port, accepts one connection on a server thread, and runs a client in the
// main thread that drives a resting-ask, crossing-bid workload and reports wire
// throughput and round-trip latency. With --listen PORT it instead serves
// connections on PORT for an external client such as netcat.
//
// The wire format is host byte order, sufficient for a loopback or same-arch
// link; a cross-arch deployment would normalise endianness at the boundary.
//
// The same records also travel over a shared-memory channel (lob/shm_channel.hpp)
// with --shm, --shm-serve and --shm-client. A local client has no use for the
// host network stack, and the channel removes it: the two processes exchange
// records through memory they both map, with no system call on the message
// path. Running --shm against the default self-test measures what that is
// worth, both times through the same engine and the same records.

#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/latency_histogram.hpp>
#include <lob/messages.hpp>
#include <lob/shm_channel.hpp>
#include <lob/spin.hpp>
#include <lob/tsc.hpp>
#include <lob/types.hpp>

#include "wire.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using lob_gateway::accum_pub;
using lob_gateway::ack_processed;
using lob_gateway::apply_order;
using lob_gateway::wire_ack;
using lob_gateway::wire_order;

constexpr std::size_t ticks = 4096;
constexpr std::size_t max_orders = std::size_t{1} << 16;
constexpr lob::tick_t mid = ticks / 2;

using lob::cpu_relax;
using lob::read_tsc;

// EAGAIN and EWOULDBLOCK are the same value on Linux, so testing both with a
// logical or is a tautology gcc flags under -Wlogical-op. Collapse to one test
// where they are equal, and keep both where a platform defines them apart.
[[nodiscard]] bool would_block(int e) noexcept {
#if EAGAIN == EWOULDBLOCK
    return e == EAGAIN;
#else
    return e == EAGAIN || e == EWOULDBLOCK;
#endif
}

void set_nonblocking(int fd) noexcept {
    const int fl = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

// Busy-polled blocking read. Spins on EAGAIN rather than sleeping, so a round
// trip is not charged the scheduler's wake-up latency, the dominant cost of a
// closed-loop link otherwise. Returns false on peer close or a real error.
bool read_all(int fd, void* buf, std::size_t n) noexcept {
    auto* p = static_cast<std::uint8_t*>(buf);
    while (n > 0) {
        const auto r = ::read(fd, p, n);
        if (r > 0) {
            p += r;
            n -= static_cast<std::size_t>(r);
            continue;
        }
        if (r == 0)
            return false;
        if (would_block(errno)) {
            cpu_relax();
            continue;
        }
        return false;
    }
    return true;
}

bool write_all(int fd, const void* buf, std::size_t n) noexcept {
    const auto* p = static_cast<const std::uint8_t*>(buf);
    while (n > 0) {
        const auto w = ::write(fd, p, n);
        if (w > 0) {
            p += w;
            n -= static_cast<std::size_t>(w);
            continue;
        }
        if (w == 0)
            return false;
        if (would_block(errno)) {
            cpu_relax();
            continue;
        }
        return false;
    }
    return true;
}

// Records staged per recv. One read fills the buffer with up to this many
// orders, the engine runs all of them, and one write returns their acks, so a
// pipelined client pays two syscalls per batch where a record-at-a-time loop
// paid two per order. A closed-loop client that waits for each ack still works,
// it simply fills one record per read and acks one per write.
constexpr std::size_t io_batch = 256;

// Reads orders from one connection, runs each through the engine, and writes an
// ack per order. Returns when the peer closes the connection.
//
// One recv stages as many whole orders as the socket has ready, the engine runs
// the whole batch, and one write returns every ack. A recv can split the final
// order across a buffer boundary, so the trailing partial-record bytes carry to
// the front of the buffer and complete on the next read. Order is preserved, so
// a resting order still rests before the order that crosses it in the same
// batch.
void serve_connection(int fd) {
    // Without TCP_NODELAY the ack write would wait on Nagle and delayed-ack,
    // adding tens of milliseconds to every round trip.
    const int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    set_nonblocking(fd);
    accum_pub pub;
    const auto eng =
        std::make_unique<lob::engine<accum_pub, ticks, max_orders>>(pub, lob::engine_config{});

    std::array<std::uint8_t, io_batch * sizeof(wire_order)> rbuf{};
    std::array<wire_ack, io_batch> acks{};
    std::size_t carry = 0;  // trailing partial-record bytes held at rbuf front

    for (;;) {
        const auto r = ::read(fd, rbuf.data() + carry, rbuf.size() - carry);
        if (r == 0)
            return;  // peer closed
        if (r < 0) {
            if (would_block(errno)) {
                cpu_relax();
                continue;
            }
            return;
        }
        const std::size_t avail = carry + static_cast<std::size_t>(r);
        std::size_t off = 0;
        std::size_t n = 0;
        while (avail - off >= sizeof(wire_order)) {
            wire_order wo{};
            std::memcpy(&wo, rbuf.data() + off, sizeof wo);
            off += sizeof(wire_order);
            apply_order(*eng, pub, wo, acks[n]);
            ++n;
        }
        carry = avail - off;
        if (carry > 0)
            std::memmove(rbuf.data(), rbuf.data() + off, carry);
        if (n > 0 && !write_all(fd, acks.data(), n * sizeof(wire_ack)))
            return;
    }
}

[[nodiscard]] int listen_socket(std::uint16_t port, std::uint16_t& bound_port) {
    const int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0)
        return -1;
    const int one = 1;
    ::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(lfd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0 ||  // NOLINT
        ::listen(lfd, 16) != 0) {
        ::close(lfd);
        return -1;
    }
    socklen_t len = sizeof addr;
    ::getsockname(lfd, reinterpret_cast<sockaddr*>(&addr), &len);  // NOLINT
    bound_port = ntohs(addr.sin_port);
    return lfd;
}

void send_order(int fd, wire_order wo, wire_ack& ack) {
    write_all(fd, &wo, sizeof wo);
    read_all(fd, &ack, sizeof ack);
}

// Reports one closed-loop run. Shared by the socket and shared-memory clients
// so the two transports are read off the same numbers in the same units.
// Returns true when every crossing bid filled.
bool report_round_trip(const char* link,
                       std::uint64_t fills,
                       std::uint64_t pairs,
                       std::uint64_t submitted,
                       double secs,
                       const lob::latency_histogram& hist) {
    const bool correct = fills == pairs;
    std::printf("correctness: %llu of %llu crossing bids filled [%s]\n",
                static_cast<unsigned long long>(fills), static_cast<unsigned long long>(pairs),
                correct ? "OK" : "MISMATCH");
    std::printf("%s throughput: orders=%llu  wall=%.3fs  %.3f Morders/s (closed loop)\n", link,
                static_cast<unsigned long long>(submitted), secs,
                static_cast<double>(submitted) / secs / 1e6);
    std::printf("%s round-trip latency (reference cycles): p50=%llu p99=%llu max=%llu\n", link,
                static_cast<unsigned long long>(hist.value_at_percentile(50.0)),
                static_cast<unsigned long long>(hist.value_at_percentile(99.0)),
                static_cast<unsigned long long>(hist.max()));
    if (hist.overflow_count() > 0) {
        std::printf(
            "latency WARNING: %llu samples exceeded the histogram range; "
            "max and upper percentiles understate the true tail\n",
            static_cast<unsigned long long>(hist.overflow_count()));
    }
    return correct;
}

// Connects to the local gateway and drives a resting-ask, crossing-bid workload
// closed loop, one order in flight, so each ack is read before the next order
// is sent. Reports wire throughput and the bid round-trip latency.
int run_client(std::uint16_t port, std::uint64_t orders) {
    const int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0)
        return 1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    while (::connect(cfd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {  // NOLINT
        std::this_thread::yield();
    }
    const int one = 1;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    set_nonblocking(cfd);

    lob::latency_histogram hist{10'000'000, 3};
    lob::order_id_t next = 1;
    const std::uint64_t pairs = orders / 2;
    std::uint64_t submitted = 0;
    std::uint64_t fills = 0;
    wire_ack ack{};
    const auto wall0 = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < pairs; ++i) {
        send_order(cfd,
                   wire_order{.id = next++,
                              .qty = 1,
                              .px = mid,
                              .new_px = 0,
                              .op = 0,
                              .side = 1,
                              .tif = 0,
                              .pad = 0},
                   ack);
        const wire_order b{
            .id = next++, .qty = 1, .px = mid, .new_px = 0, .op = 0, .side = 0, .tif = 1, .pad = 0};
        const auto t0 = read_tsc();
        send_order(cfd, b, ack);
        hist.record(read_tsc() - t0);
        fills += ack.filled;
        submitted += 2;
    }
    const auto wall1 = std::chrono::steady_clock::now();
    ::close(cfd);

    const double secs = std::chrono::duration<double>(wall1 - wall0).count();
    // The round trip is dominated by the host network stack and scheduling. On a
    // virtualised host it is hundreds of microseconds; on bare metal with
    // isolated, busy-polled cores it is single-digit microseconds. The engine's
    // own latency is the in-process figure from lob_loadgen, and the same
    // workload over the shared-memory channel is the --shm mode below.
    return report_round_trip("wire", fills, pairs, submitted, secs, hist) ? 0 : 1;
}

// Connects to the local gateway and drives the same resting-ask, crossing-bid
// workload as run_client, but pipelined: a window of orders is written in one
// batch and the matching window of acks is read back, so the gateway stages the
// whole window in one recv and returns its acks in one write. This is the path
// the batched serve_connection is built for, and it measures sustained wire
// throughput rather than the one-in-flight round trip. The window is bounded so
// the in-flight bytes stay well inside the socket buffers and the two sides
// never deadlock writing to each other.
int run_pipeline_client(std::uint16_t port, std::uint64_t orders, std::size_t window) {
    const int cfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (cfd < 0)
        return 1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    while (::connect(cfd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {  // NOLINT
        std::this_thread::yield();
    }
    const int one = 1;
    ::setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    set_nonblocking(cfd);

    const std::uint64_t pairs = orders / 2;
    // The window bounds the bytes in flight on the loopback socket. Orders
    // out and acks back total 112 bytes per pair, so 512 pairs keeps under
    // 60 KiB in each direction, inside any default socket buffer. An
    // unclamped window would let both sides block writing to each other.
    constexpr std::size_t max_window = 512;
    const std::size_t win = std::clamp<std::size_t>(window, 1, max_window);
    lob::order_id_t next = 1;
    std::uint64_t submitted = 0;
    std::uint64_t fills = 0;
    std::vector<wire_order> obuf;
    std::vector<wire_ack> abuf;
    const auto wall0 = std::chrono::steady_clock::now();
    for (std::uint64_t done = 0; done < pairs;) {
        const std::uint64_t w = std::min<std::uint64_t>(win, pairs - done);
        obuf.clear();
        for (std::uint64_t i = 0; i < w; ++i) {
            obuf.push_back(wire_order{.id = next++,
                                      .qty = 1,
                                      .px = mid,
                                      .new_px = 0,
                                      .op = 0,
                                      .side = 1,
                                      .tif = 0,
                                      .pad = 0});
            obuf.push_back(wire_order{.id = next++,
                                      .qty = 1,
                                      .px = mid,
                                      .new_px = 0,
                                      .op = 0,
                                      .side = 0,
                                      .tif = 1,
                                      .pad = 0});
        }
        if (!write_all(cfd, obuf.data(), obuf.size() * sizeof(wire_order)))
            break;
        abuf.resize(obuf.size());
        if (!read_all(cfd, abuf.data(), abuf.size() * sizeof(wire_ack)))
            break;
        fills = std::transform_reduce(abuf.begin(), abuf.end(), fills, std::plus<>{},
                                      [](const wire_ack& a) { return a.filled; });
        submitted += obuf.size();
        done += w;
    }
    const auto wall1 = std::chrono::steady_clock::now();
    ::close(cfd);

    const double secs = std::chrono::duration<double>(wall1 - wall0).count();
    const bool correct = fills == pairs;
    std::printf("correctness: %llu of %llu crossing bids filled [%s]\n",
                static_cast<unsigned long long>(fills), static_cast<unsigned long long>(pairs),
                correct ? "OK" : "MISMATCH");
    std::printf(
        "wire throughput: orders=%llu  wall=%.3fs  %.3f Morders/s (pipelined, window=%zu)\n",
        static_cast<unsigned long long>(submitted), secs,
        static_cast<double>(submitted) / secs / 1e6, win);
    return correct ? 0 : 1;
}

// --- shared-memory transport -------------------------------------------------
//
// Same protocol records as the socket path, carried by a pair of rings the two
// processes map rather than by the host network stack. See lob/shm_channel.hpp
// for the LRPC and URPC argument the arrangement comes from.

// Requests and replies in flight. The closed-loop client keeps one order in
// flight and a pipelined one a few hundred, so the ring only has to absorb a
// burst, and at 32 and 24 bytes a record the whole channel stays well inside
// the last-level cache.
constexpr std::size_t shm_ring_capacity = 1024;

using shm_channel_t = lob::shm_channel<wire_order, wire_ack, shm_ring_capacity>;

// A socket client signals its departure by closing the connection. The channel
// has no such event, so the last request carries this op and the server acks it
// and stops. Any other value is a command.
constexpr std::uint8_t op_disconnect = 0xFF;

constexpr std::string_view default_shm_name = "/lob-gateway";

// Spins waiting for the peer to publish the channel. Generous, because it
// covers a client started before the server, and bounded, so a client whose
// server never arrives exits rather than spinning forever.
constexpr std::uint64_t shm_attach_spins = 2'000'000'000;

// Runs orders off the request ring until a disconnect arrives. The reply ring
// is sized to the request ring, and the client keeps fewer orders in flight
// than either holds, so the push cannot fail with a well-behaved client; a
// full ring is a client that stopped reading, and the spin applies the
// backpressure the socket path gets from its send buffer.
void serve_shm(shm_channel_t& ch) {
    accum_pub pub;
    const auto eng =
        std::make_unique<lob::engine<accum_pub, ticks, max_orders>>(pub, lob::engine_config{});
    wire_order wo{};
    wire_ack ack{};
    for (;;) {
        if (!ch.requests.try_pop(wo)) {
            cpu_relax();
            continue;
        }
        if (wo.op == op_disconnect) {
            ack = wire_ack{.id = wo.id, .filled = 0, .last_px = 0, .status = ack_processed};
            while (!ch.replies.try_push(ack))
                cpu_relax();
            return;
        }
        apply_order(*eng, pub, wo, ack);
        while (!ch.replies.try_push(ack))
            cpu_relax();
    }
}

// Drives the same resting-ask, crossing-bid workload as run_client, closed
// loop, over the channel instead of a socket.
int run_shm_client(shm_channel_t& ch, std::uint64_t orders) {
    lob::latency_histogram hist{10'000'000, 3};
    lob::order_id_t next = 1;
    const std::uint64_t pairs = orders / 2;
    std::uint64_t submitted = 0;
    std::uint64_t fills = 0;
    wire_ack ack{};
    const auto send = [&ch, &ack](const wire_order& wo) noexcept {
        while (!ch.requests.try_push(wo))
            cpu_relax();
        while (!ch.replies.try_pop(ack))
            cpu_relax();
    };
    const auto wall0 = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < pairs; ++i) {
        send(wire_order{.id = next++,
                        .qty = 1,
                        .px = mid,
                        .new_px = 0,
                        .op = 0,
                        .side = 1,
                        .tif = 0,
                        .pad = 0});
        const wire_order b{
            .id = next++, .qty = 1, .px = mid, .new_px = 0, .op = 0, .side = 0, .tif = 1, .pad = 0};
        const auto t0 = read_tsc();
        send(b);
        hist.record(read_tsc() - t0);
        fills += ack.filled;
        submitted += 2;
    }
    const auto wall1 = std::chrono::steady_clock::now();
    send(wire_order{.id = next,
                    .qty = 0,
                    .px = 0,
                    .new_px = 0,
                    .op = op_disconnect,
                    .side = 0,
                    .tif = 0,
                    .pad = 0});

    const double secs = std::chrono::duration<double>(wall1 - wall0).count();
    return report_round_trip("shm", fills, pairs, submitted, secs, hist) ? 0 : 1;
}

struct args {
    std::uint64_t orders{50'000};
    int listen_port{-1};
    std::size_t pipeline{0};  // 0 = closed loop; >0 = pipelined window in pairs
    std::string shm_name{};
    bool shm{false};         // self-test over a shared-memory channel
    bool shm_serve{false};   // serve one channel and exit
    bool shm_client{false};  // drive a channel another process serves
    bool help{false};
};

args parse_args(int argc, char** argv) {
    args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--orders" && i + 1 < argc) {
            a.orders = std::strtoull(argv[++i], nullptr, 10);
        } else if (s == "--listen" && i + 1 < argc) {
            a.listen_port = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (s == "--pipeline" && i + 1 < argc) {
            a.pipeline = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (s == "--shm") {
            a.shm = true;
        } else if (s == "--shm-serve") {
            a.shm_serve = true;
        } else if (s == "--shm-client") {
            a.shm_client = true;
        } else if (s == "--shm-name" && i + 1 < argc) {
            a.shm_name = argv[++i];
        } else if (s == "--help") {
            a.help = true;
        }
    }
    return a;
}

// The body of main, which allocates the engine and starts threads and so can
// throw. main catches, so a failure prints rather than terminating.
int run(const args& a) {
    if (a.help) {
        std::printf(
            "usage: lob_gateway [--orders N] [--listen PORT] [--pipeline W]\n"
            "                   [--shm | --shm-serve | --shm-client] [--shm-name NAME]\n"
            "  --orders N    orders for the self-test (default 50000)\n"
            "  --listen PORT serve connections on PORT instead of self-testing\n"
            "  --pipeline W  self-test with a pipelined client, window W pairs\n"
            "  --shm         self-test over a shared-memory channel instead of TCP\n"
            "  --shm-serve   serve one shared-memory channel and exit\n"
            "  --shm-client  drive a shared-memory channel another process serves\n"
            "  --shm-name N  segment name for the channel (default %.*s)\n",
            static_cast<int>(default_shm_name.size()), default_shm_name.data());
        return 0;
    }

    if (a.shm || a.shm_serve || a.shm_client) {
        const std::string name = a.shm_name.empty() ? std::string{default_shm_name} : a.shm_name;
        constexpr std::size_t bytes = lob::shm_channel_bytes<shm_channel_t>();

        // The server owns the segment, so it creates and unlinks it; a client
        // attaches to what the server published. The self-test does both, in
        // two independent mappings of the one segment, so the rings are
        // reached exactly as two processes reach them.
        if (a.shm_client) {
            const lob::shm_region region = lob::shm_region::attach(name, bytes);
            if (!region.valid()) {
                std::fprintf(stderr, "gateway: shm attach failed (errno %d)\n", region.error());
                return 1;
            }
            auto* ch = lob::shm_channel_view<shm_channel_t>(region);
            if (!lob::shm_channel_wait(ch, shm_attach_spins)) {
                std::fprintf(stderr, "gateway: shm channel never became ready\n");
                return 1;
            }
            return run_shm_client(*ch, a.orders);
        }

        const lob::shm_region region = lob::shm_region::create(name, bytes);
        if (!region.valid()) {
            std::fprintf(stderr, "gateway: shm create failed (errno %d)\n", region.error());
            return 1;
        }
        auto* ch = lob::shm_channel_init<shm_channel_t>(region);
        if (ch == nullptr) {
            std::fprintf(stderr, "gateway: shm channel init failed\n");
            return 1;
        }
        if (a.shm_serve) {
            std::printf("gateway: serving shared-memory channel %s\n", name.c_str());
            serve_shm(*ch);
            return 0;
        }

        std::thread server([ch] { serve_shm(*ch); });
        const lob::shm_region client_region = lob::shm_region::attach(name, bytes);
        int rc = 1;
        if (!client_region.valid()) {
            std::fprintf(stderr, "gateway: shm attach failed (errno %d)\n", client_region.error());
        } else {
            auto* cch = lob::shm_channel_view<shm_channel_t>(client_region);
            rc = lob::shm_channel_wait(cch, shm_attach_spins) ? run_shm_client(*cch, a.orders) : 1;
        }
        server.join();
        return rc;
    }

    if (a.listen_port >= 0) {
        if (a.listen_port > 65535) {
            std::fprintf(stderr, "gateway: --listen port must be 0..65535\n");
            return 1;
        }
        std::uint16_t bound = 0;
        const int lfd = listen_socket(static_cast<std::uint16_t>(a.listen_port), bound);
        if (lfd < 0) {
            std::fprintf(stderr, "gateway: listen failed\n");
            return 1;
        }
        std::printf("gateway: listening on 127.0.0.1:%u\n", bound);
        for (;;) {
            const int cfd = ::accept(lfd, nullptr, nullptr);
            if (cfd < 0)
                break;
            serve_connection(cfd);
            ::close(cfd);
        }
        ::close(lfd);
        return 0;
    }

    // Self-test: listen on an ephemeral port, serve on a thread, drive a client.
    std::uint16_t port = 0;
    const int lfd = listen_socket(0, port);
    if (lfd < 0) {
        std::fprintf(stderr, "gateway: listen failed\n");
        return 1;
    }
    std::thread server([lfd] {
        const int cfd = ::accept(lfd, nullptr, nullptr);
        if (cfd >= 0) {
            serve_connection(cfd);
            ::close(cfd);
        }
    });
    const int rc = a.pipeline > 0 ? run_pipeline_client(port, a.orders, a.pipeline)
                                  : run_client(port, a.orders);
    server.join();
    ::close(lfd);
    return rc;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "gateway: %s\n", e.what());
        return 1;
    }
}
