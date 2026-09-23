#ifndef LOB_SHM_CHANNEL_HPP
#define LOB_SHM_CHANNEL_HPP

#include <lob/spin.hpp>
#include <lob/spsc_ring.hpp>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <string_view>

#if defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace lob {

// Shared-memory order-entry channel: a request ring and a reply ring that two
// processes on one machine map into their own address spaces.
//
// A local client reaching the engine over TCP pays the whole host network
// stack for a message the two processes could have exchanged through memory
// they both map. Bershad, Anderson, Lazowska and Levy measured that gap
// directly (doi:10.1145/77648.77650): the overwhelming majority of remote
// procedure calls never leave the machine, the arguments they carry are small
// and of fixed size, and a call implemented for the cross-machine case pays
// stub interpretation, message buffer management, access validation, dispatch
// and a scheduler rendezvous that a same-machine call does not need. Their
// LRPC removes each of those, and the follow-up URPC (doi:10.1145/103720.114701)
// removes the kernel from the call path entirely by passing messages through
// memory mapped pairwise between the two address spaces.
//
// This channel is the same arrangement in the order-entry direction:
//
//   - Bind once. The segment is created, sized and mapped when a client
//     attaches. Nothing on the message path afterwards enters the kernel:
//     no read, no write, no wake-up.
//   - One copy. The client writes the request straight into the ring slot the
//     server will read, as LRPC writes arguments into the shared A-stack, in
//     place of the four copies a message-based path makes.
//   - No dispatch layer. The server polls its own ring and runs the command;
//     there is no message to examine and no thread to select, the indirection
//     LRPC names as the cost of bridging abstract and concrete threads.
//   - The server context is already resident. The serving thread spins on its
//     own core, which is what LRPC buys by caching domains on idle processors,
//     except that here it is the steady state rather than an optimisation.
//   - No shared structure between channels. Each client gets its own pair of
//     rings, so two clients never touch the same line (LRPC section 3.4).
//
// Both rings are plain lob::spsc_ring objects living inside the mapping, so
// the queue discipline, the sequence-per-slot handoff and the cache behaviour
// are the ones the in-process rings already have and the tests already cover.
// The owner constructs them and publishes a ready word; the peer maps the same
// segment and reads the objects in place.
//
// The payload types must be trivially copyable and must have the same layout
// in both processes, which holds for the fixed-size wire records the gateway
// already defines. The segment carries no pointers, so the two mappings need
// not land at the same address.

// One POSIX shared-memory segment, mapped read-write.
//
// create() makes the segment and owns its name: the destructor unlinks it, so
// a crashed server leaves no segment behind for the next run to inherit.
// attach() maps a segment another process created and leaves the name alone.
// A failed call yields an invalid region carrying the errno that failed it.
class shm_region {
   public:
    shm_region() noexcept = default;

    shm_region(const shm_region&) = delete;
    shm_region& operator=(const shm_region&) = delete;

    shm_region(shm_region&& other) noexcept
        : addr_(other.addr_),
          bytes_(other.bytes_),
          name_(std::move(other.name_)),
          err_(other.err_),
          owner_(other.owner_) {
        other.addr_ = nullptr;
        other.bytes_ = 0;
        other.owner_ = false;
    }

    shm_region& operator=(shm_region&& other) noexcept {
        if (this != &other) {
            release_();
            addr_ = other.addr_;
            bytes_ = other.bytes_;
            name_ = std::move(other.name_);
            err_ = other.err_;
            owner_ = other.owner_;
            other.addr_ = nullptr;
            other.bytes_ = 0;
            other.owner_ = false;
        }
        return *this;
    }

    ~shm_region() { release_(); }

    [[nodiscard]] static shm_region create(std::string_view name, std::size_t bytes) noexcept {
        return open_(name, bytes, true);
    }

    [[nodiscard]] static shm_region attach(std::string_view name, std::size_t bytes) noexcept {
        return open_(name, bytes, false);
    }

    [[nodiscard]] void* data() const noexcept { return addr_; }

    [[nodiscard]] std::size_t size() const noexcept { return bytes_; }

    [[nodiscard]] bool valid() const noexcept { return addr_ != nullptr; }

    // errno from the call that failed, or 0 when the region is valid.
    [[nodiscard]] int error() const noexcept { return err_; }

   private:
    [[nodiscard]] static shm_region open_(std::string_view name,
                                          std::size_t bytes,
                                          bool create) noexcept {
        shm_region r;
        // POSIX names a segment with a leading slash and no other, and the
        // cap keeps a long name from being silently truncated by the platform.
        if (bytes == 0 || name.size() < 2 || name.size() > 200 || name.front() != '/' ||
            name.find('/', 1) != std::string_view::npos) {
            r.err_ = EINVAL;
            return r;
        }
#if defined(__linux__) || defined(__APPLE__)
        const std::string nm{name};
        int fd = -1;
        if (create) {
            // A segment left by a previous run would be mapped with its stale
            // contents, so unlink first and create exclusively. ENOENT here is
            // the expected case rather than a failure.
            ::shm_unlink(nm.c_str());
            fd = ::shm_open(nm.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
            if (fd >= 0 && ::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
                r.err_ = errno;
                ::close(fd);
                ::shm_unlink(nm.c_str());
                return r;
            }
        } else {
            fd = ::shm_open(nm.c_str(), O_RDWR, 0);
            if (fd >= 0) {
                // The peer must not map more than the owner sized, which would
                // fault past the end of the segment on first touch.
                struct stat st{};
                const bool sized = ::fstat(fd, &st) == 0;
                if (!sized || static_cast<std::size_t>(st.st_size) < bytes) {
                    r.err_ = sized ? EINVAL : errno;
                    ::close(fd);
                    return r;
                }
            }
        }
        if (fd < 0) {
            r.err_ = errno;
            return r;
        }
        void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        // The mapping keeps the segment alive, so the descriptor is not needed
        // past this point and holding it would leak one per attach.
        ::close(fd);
        if (p == MAP_FAILED) {
            r.err_ = errno;
            if (create)
                ::shm_unlink(nm.c_str());
            return r;
        }
        r.addr_ = p;
        r.bytes_ = bytes;
        r.owner_ = create;
        if (create)
            r.name_ = nm;
        return r;
#else
        r.err_ = ENOSYS;
        return r;
#endif
    }

    void release_() noexcept {
#if defined(__linux__) || defined(__APPLE__)
        if (addr_ != nullptr)
            ::munmap(addr_, bytes_);
        if (owner_ && !name_.empty())
            ::shm_unlink(name_.c_str());
#endif
        addr_ = nullptr;
        bytes_ = 0;
        owner_ = false;
    }

    void* addr_{nullptr};
    std::size_t bytes_{0};
    std::string name_{};
    int err_{0};
    bool owner_{false};
};

// The object that lives in the segment: a ready word the peer waits on and the
// two rings. Requests travel client to server, replies server to client, so
// each ring has exactly one producer process and one consumer process, which
// is the contract spsc_ring already states.
template <class Req, class Rsp, std::size_t Capacity>
struct shm_channel {
    static_assert(std::is_trivially_copyable_v<Req>, "shm_channel: Req must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<Rsp>, "shm_channel: Rsp must be trivially copyable");

    // "LOBSHM01" read as bytes. The peer spins until it sees this word, which
    // separates a mapping of a segment whose rings are constructed from one
    // the owner has only sized.
    static constexpr std::uint64_t magic = 0x4C4F4253484D3031ULL;

    std::atomic<std::uint64_t> ready{0};
    spsc_ring<Req, Capacity> requests{};
    spsc_ring<Rsp, Capacity> replies{};
};

// Bytes to size a segment for one channel.
template <class Ch>
[[nodiscard]] constexpr std::size_t shm_channel_bytes() noexcept {
    return sizeof(Ch);
}

// Construct the channel in a freshly created region and publish it. Returns
// nullptr when the region is invalid or too small for the channel.
template <class Ch>
[[nodiscard]] Ch* shm_channel_init(const shm_region& region) noexcept {
    if (!region.valid() || region.size() < sizeof(Ch))
        return nullptr;
    Ch* ch = std::construct_at(static_cast<Ch*>(region.data()));
    // Release so a peer that sees the magic also sees constructed rings.
    ch->ready.store(Ch::magic, std::memory_order_release);
    return ch;
}

// View the channel an owner constructed in a segment this process mapped. The
// caller waits for readiness before using the rings.
template <class Ch>
[[nodiscard]] Ch* shm_channel_view(const shm_region& region) noexcept {
    if (!region.valid() || region.size() < sizeof(Ch))
        return nullptr;
    return std::launder(reinterpret_cast<Ch*>(region.data()));
}

// Spin until the owner publishes the channel, up to spins polls. Returns false
// if the word never appeared, which a caller treats as a failed attach rather
// than waiting for a server that may never arrive.
template <class Ch>
[[nodiscard]] bool shm_channel_wait(const Ch* ch, std::uint64_t spins) noexcept {
    for (std::uint64_t i = 0; i < spins; ++i) {
        if (ch != nullptr && ch->ready.load(std::memory_order_acquire) == Ch::magic)
            return true;
        cpu_relax();
    }
    return false;
}

}  // namespace lob

#endif  // LOB_SHM_CHANNEL_HPP
