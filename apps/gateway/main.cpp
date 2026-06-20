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

#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/latency_histogram.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::size_t ticks = 4096;
constexpr std::size_t max_orders = std::size_t{1} << 16;
constexpr lob::tick_t mid = ticks / 2;

// Client -> gateway. Fixed layout, trivially copyable, read straight off the
// socket. op selects the command; new_px is used only by modify.
struct wire_order {
    std::uint64_t id;
    std::uint64_t qty;
    std::uint32_t px;
    std::uint32_t new_px;
    std::uint8_t op;    // 0 submit, 1 cancel, 2 modify
    std::uint8_t side;  // 0 bid, 1 ask
    std::uint8_t tif;   // 0 gtc, 1 ioc, 2 fok
    std::uint8_t pad;
};

static_assert(sizeof(wire_order) == 32);
static_assert(std::is_trivially_copyable_v<wire_order>);

// Gateway -> client. One per order. filled and last_px summarise the order's
// fills; status is 0 accepted, 1 filled, 2 cancel or modify processed.
struct wire_ack {
    std::uint64_t id;
    std::uint64_t filled;
    std::uint32_t last_px;
    std::uint32_t status;
};

static_assert(sizeof(wire_ack) == 24);
static_assert(std::is_trivially_copyable_v<wire_ack>);

// Accumulates one order's fills so the gateway can summarise them in the ack.
struct accum_pub {
    std::uint64_t filled{0};
    lob::tick_t last_px{0};

    void publish(const lob::fill_msg& f) noexcept {
        filled += f.qty;
        last_px = f.px;
    }

    void publish(const lob::top_msg&) noexcept {}

    void publish(const lob::trade_msg&) noexcept {}

    void publish(const lob::self_trade_msg&) noexcept {}

    void reset() noexcept {
        filled = 0;
        last_px = 0;
    }
};

[[nodiscard]] std::uint64_t now_tsc() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    return __builtin_ia32_rdtsc();
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

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

// Reads orders from one connection, runs each through the engine, and writes an
// ack per order. Returns when the peer closes the connection.
void serve_connection(int fd) {
    // Without TCP_NODELAY the ack write would wait on Nagle and delayed-ack,
    // adding tens of milliseconds to every round trip.
    const int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    set_nonblocking(fd);
    accum_pub pub;
    const auto eng =
        std::make_unique<lob::engine<accum_pub, ticks, max_orders>>(pub, lob::engine_config{});
    wire_order wo{};
    while (read_all(fd, &wo, sizeof wo)) {
        pub.reset();
        std::uint32_t status = 0;
        switch (wo.op) {
        case 0:
            eng->on_submit(lob::submit_msg{.id = wo.id,
                                           .px = wo.px,
                                           .qty = wo.qty,
                                           .s = wo.side == 0 ? lob::side::bid : lob::side::ask,
                                           .t = static_cast<lob::tif>(wo.tif),
                                           ._pad = 0,
                                           .account_id = 0});
            status = pub.filled > 0 ? 1U : 0U;
            break;
        case 1:
            eng->on_cancel(lob::cancel_msg{.id = wo.id});
            status = 2;
            break;
        default:
            eng->on_modify(lob::modify_msg{.id = wo.id, .new_px = wo.new_px, .new_qty = wo.qty});
            status = 2;
            break;
        }
        const wire_ack ack{
            .id = wo.id, .filled = pub.filled, .last_px = pub.last_px, .status = status};
        if (!write_all(fd, &ack, sizeof ack))
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
        const auto t0 = now_tsc();
        send_order(cfd, b, ack);
        hist.record(now_tsc() - t0);
        fills += ack.filled;
        submitted += 2;
    }
    const auto wall1 = std::chrono::steady_clock::now();
    ::close(cfd);

    const double secs = std::chrono::duration<double>(wall1 - wall0).count();
    const bool correct = fills == pairs;
    std::printf("correctness: %llu of %llu crossing bids filled [%s]\n",
                static_cast<unsigned long long>(fills),
                static_cast<unsigned long long>(pairs),
                correct ? "OK" : "MISMATCH");
    std::printf("wire throughput: orders=%llu  wall=%.3fs  %.3f Morders/s (closed loop)\n",
                static_cast<unsigned long long>(submitted),
                secs,
                static_cast<double>(submitted) / secs / 1e6);
    // The round trip is dominated by the host network stack and scheduling. On a
    // virtualised host it is hundreds of microseconds; on bare metal with
    // isolated, busy-polled cores it is single-digit microseconds. The engine's
    // own latency is the in-process figure from lob_loadgen.
    std::printf("wire round-trip latency (reference cycles): p50=%llu p99=%llu max=%llu\n",
                static_cast<unsigned long long>(hist.value_at_percentile(50.0)),
                static_cast<unsigned long long>(hist.value_at_percentile(99.0)),
                static_cast<unsigned long long>(hist.max()));
    return correct ? 0 : 1;
}

struct args {
    std::uint64_t orders{50'000};
    int listen_port{-1};
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
        } else if (s == "--help") {
            a.help = true;
        }
    }
    return a;
}

}  // namespace

int main(int argc, char** argv) {
    const args a = parse_args(argc, argv);
    if (a.help) {
        std::printf("usage: lob_gateway [--orders N] [--listen PORT]\n"
                    "  --orders N    orders for the self-test (default 2000000)\n"
                    "  --listen PORT serve connections on PORT instead of self-testing\n");
        return 0;
    }

    if (a.listen_port >= 0) {
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
    const int rc = run_client(port, a.orders);
    server.join();
    ::close(lfd);
    return rc;
}
