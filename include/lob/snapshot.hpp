#ifndef LOB_SNAPSHOT_HPP
#define LOB_SNAPSHOT_HPP

#include <lob/types.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace lob {

// On-the-wire layout of a snapshot:
//
//   [snapshot_header] [snapshot_order_record] x N
//
// Sizes and alignment are fixed and explicit so the format is portable
// across compilers. The header pins the engine's template parameters
// (Ticks, MaxOrders); restore() must be called against an engine
// instantiated with identical template arguments or it rejects the blob.
//
// All integers are stored little-endian via std::memcpy from the
// host-native representation. The engine and the snapshot share one
// process, so byte order matches by construction; persistence across
// hosts would need an endianness conversion layer (out of scope here).
struct snapshot_header {
    static constexpr std::array<char, 4> magic_bytes{'L', 'O', 'B', 'S'};
    static constexpr std::uint32_t       wire_version{1};

    std::array<char, 4> magic{magic_bytes};
    std::uint32_t       version{wire_version};
    std::uint64_t       ticks{0};
    std::uint64_t       max_orders{0};
    std::uint8_t        self_cross{0};
    std::uint8_t        top_throttle{0};
    std::uint16_t       _pad0{0};
    std::uint32_t       _pad1{0};
    seq_t               seq{0};
    tick_t              last_bid_px{0};
    tick_t              last_ask_px{0};
    qty_t               last_bid_qty{0};
    qty_t               last_ask_qty{0};
    std::uint8_t        have_top{0};
    std::uint8_t        _pad2[7]{};
    std::uint64_t       num_orders{0};
};
static_assert(sizeof(snapshot_header) == 80);
static_assert(std::is_trivially_copyable_v<snapshot_header>);

struct snapshot_order_record {
    order_id_t   id;
    qty_t        remaining;
    tick_t       px;
    std::uint8_t s;
    std::uint8_t t;
    std::uint16_t _pad0{0};
    account_id_t account_id;
};
static_assert(sizeof(snapshot_order_record) == 32);
static_assert(std::is_trivially_copyable_v<snapshot_order_record>);

template <class S>
concept snapshot_sink = requires(S s, std::span<std::byte const> bytes) {
    { s.write(bytes) } noexcept -> std::same_as<void>;
};

template <class R>
concept snapshot_source = requires(R r, std::span<std::byte> bytes) {
    { r.read(bytes) } noexcept -> std::same_as<bool>;
};

// In-memory byte vector that satisfies both snapshot_sink and
// snapshot_source. Test fixtures and round-trip benchmarks use this.
class vector_snapshot_buffer {
public:
    void write(std::span<std::byte const> bytes) noexcept {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] bool read(std::span<std::byte> bytes) noexcept {
        if (cursor_ + bytes.size() > bytes_.size())
            return false;
        std::memcpy(bytes.data(), bytes_.data() + cursor_, bytes.size());
        cursor_ += bytes.size();
        return true;
    }

    void rewind() noexcept { cursor_ = 0; }
    void reset()  noexcept { bytes_.clear(); cursor_ = 0; }

    [[nodiscard]] std::size_t size()    const noexcept { return bytes_.size(); }
    [[nodiscard]] std::size_t cursor()  const noexcept { return cursor_; }

private:
    std::vector<std::byte> bytes_;
    std::size_t            cursor_{0};
};

}  // namespace lob

#endif  // LOB_SNAPSHOT_HPP
