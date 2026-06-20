#ifndef LOB_FIX_PARSER_HPP
#define LOB_FIX_PARSER_HPP

#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <system_error>

namespace lob::fix {

// Zero-copy FIX 4.4 tag-value parser for the order-entry gateway.
//
// parse() turns one FIX message in a contiguous byte buffer into a
// lob::command (submit / cancel / modify) that the engine consumes. It
// allocates nothing: every field is a std::string_view into the caller's
// buffer, the tag folds to an int inline as the field is scanned, and value
// conversion goes through std::from_chars. The parser is
// deliberately decoupled from the engine template -- it speaks the command
// tagged union, not engine<P, Ticks, MaxOrders>, so the gateway can frame
// and validate wire bytes without instantiating a book.
//
// Supported MsgTypes (the order-entry subset on the roadmap):
//   D  NewOrderSingle          -> submit_msg  (ClOrdID drives the order id)
//   F  OrderCancelRequest      -> cancel_msg  (OrigClOrdID names the resting order)
//   G  OrderCancelReplaceRequest -> modify_msg (OrigClOrdID names the resting order)
//
// Prices (tag 44) are taken as integer ticks: the engine's price domain is
// the dense [0, Ticks) ladder, so the gateway passes ticks rather than a
// decimal that would force a float conversion on the hot path.
//
// Framing: parse() validates the standard envelope -- BeginString(8),
// BodyLength(9) and the trailing CheckSum(10) -- so result::consumed reports
// the exact byte length of the message. A caller draining a TCP stream feeds
// the accumulated buffer, advances by consumed on error::ok, and waits for
// more bytes on error::incomplete. The buffer is never mutated.

enum class error : std::uint8_t {
    ok,
    incomplete,
    malformed,
    bad_begin_string,
    bad_body_length,
    bad_checksum,
    unsupported_msg_type,
    missing_field,
    bad_field_value,
};

struct result {
    error err{error::malformed};
    command cmd{};
    // Bytes consumed by this message. Meaningful when err == ok; the caller
    // advances its stream cursor by this amount before parsing the next
    // message.
    std::size_t consumed{0};
};

namespace detail {

inline constexpr char soh = '\x01';
inline constexpr std::size_t checksum_field_width = 7;  // "10=" + three digits + SOH

enum class scan : std::uint8_t { ok, need_more, bad };

struct field {
    int tag{0};
    std::string_view value{};
};

// Read one `tag=value<SOH>` field starting at pos. On scan::ok, pos is
// advanced past the terminating SOH. scan::need_more means the buffer ended
// before the field completed (a structurally valid prefix -- the caller maps
// this to error::incomplete); scan::bad means the tag was not a non-empty
// run of digits.
[[nodiscard]] inline scan read_field(std::string_view buf, std::size_t& pos, field& out) noexcept {
    const std::size_t n = buf.size();
    if (pos >= n)
        return scan::need_more;

    // Single forward pass over `tag=value<SOH>`. The tag digits are
    // accumulated as the scan walks to '=', folding the delimiter search and
    // the integer conversion that a find()-then-from_chars pair did into one
    // loop over the same bytes. Overflow rejection matches std::from_chars so
    // a tag wider than an int is malformed, not a wrapped value.
    const char* const data = buf.data();
    int tag = 0;
    std::size_t i = pos;
    for (; i < n; ++i) {
        const char ch = data[i];
        if (ch == '=')
            break;
        const unsigned digit = static_cast<unsigned>(static_cast<unsigned char>(ch)) - '0';
        if (digit >= 10)
            return scan::bad;
        constexpr int int_max = 2147483647;
        if (tag > (int_max - static_cast<int>(digit)) / 10)
            return scan::bad;
        tag = tag * 10 + static_cast<int>(digit);
    }
    if (i >= n)
        return scan::need_more;
    if (i == pos)
        return scan::bad;

    const std::size_t val_begin = i + 1;
    std::size_t soh_pos = val_begin;
    while (soh_pos < n && data[soh_pos] != soh)
        ++soh_pos;
    if (soh_pos >= n)
        return scan::need_more;

    out.tag = tag;
    out.value = buf.substr(val_begin, soh_pos - val_begin);
    pos = soh_pos + 1;
    return scan::ok;
}

// Parse a whole std::string_view as an unsigned integer, accepting the field
// only when std::from_chars consumes every byte. A measured A/B kept this on
// from_chars because a hand-rolled digit fold parsed the value fields slower,
// where the inline tag scan in read_field wins by also folding the delimiter
// search it replaces.
template <typename T>
[[nodiscard]] inline bool to_uint(std::string_view v, T& out) noexcept {
    if (v.empty())
        return false;
    const char* const begin = v.data();
    const char* const end = begin + v.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

}  // namespace detail

[[nodiscard]] inline result parse(std::span<const std::byte> in) noexcept {
    using detail::field;
    using detail::read_field;
    using detail::scan;
    using detail::soh;
    using detail::to_uint;

    result r{};
    const std::string_view buf{reinterpret_cast<const char*>(in.data()), in.size()};
    std::size_t pos = 0;
    field f{};

    // BeginString(8) must come first and pin the dialect.
    scan st = read_field(buf, pos, f);
    if (st == scan::need_more) {
        r.err = error::incomplete;
        return r;
    }
    if (st == scan::bad || f.tag != 8) {
        r.err = error::malformed;
        return r;
    }
    if (f.value != "FIX.4.4") {
        r.err = error::bad_begin_string;
        return r;
    }

    // BodyLength(9) fixes the message frame.
    st = read_field(buf, pos, f);
    if (st == scan::need_more) {
        r.err = error::incomplete;
        return r;
    }
    if (st == scan::bad || f.tag != 9) {
        r.err = error::malformed;
        return r;
    }
    std::size_t body_len = 0;
    if (!to_uint(f.value, body_len)) {
        r.err = error::bad_body_length;
        return r;
    }

    const std::size_t body_start = pos;
    if (body_len > in.size()) {
        r.err = error::incomplete;
        return r;
    }
    const std::size_t total = body_start + body_len + detail::checksum_field_width;
    if (in.size() < total) {
        r.err = error::incomplete;
        return r;
    }

    // CheckSum(10) closes the frame: "10=" + three digits + SOH.
    const std::size_t cs_start = total - detail::checksum_field_width;
    if (buf.substr(cs_start, 3) != "10=" || buf[total - 1] != soh) {
        r.err = error::malformed;
        return r;
    }
    unsigned stated = 0;
    if (!to_uint(buf.substr(cs_start + 3, 3), stated)) {
        r.err = error::malformed;
        return r;
    }
    unsigned computed = 0;
    for (std::size_t i = 0; i < cs_start; ++i)
        computed += static_cast<unsigned char>(buf[i]);
    computed &= 0xFFU;
    if (computed != stated) {
        r.err = error::bad_checksum;
        return r;
    }

    // Body fields live in [body_start, cs_start). Bounding the view here keeps
    // read_field from straying into the CheckSum field. MsgType(35) leads.
    const std::string_view body = buf.substr(0, cs_start);
    std::size_t bpos = body_start;
    field bf{};
    if (read_field(body, bpos, bf) != scan::ok || bf.tag != 35) {
        r.err = error::malformed;
        return r;
    }
    const std::string_view msg_type = bf.value;

    order_id_t clordid = 0;
    order_id_t orig_id = 0;
    qty_t qty = 0;
    tick_t px = 0;
    side sd = side::bid;
    tif tf = tif::gtc;
    account_id_t account = 0;
    bool has_clordid = false;
    bool has_orig = false;
    bool has_qty = false;
    bool has_px = false;
    bool has_side = false;

    while (bpos < cs_start) {
        if (read_field(body, bpos, bf) != scan::ok) {
            r.err = error::malformed;
            return r;
        }
        switch (bf.tag) {
        case 1:  // Account
            if (!to_uint(bf.value, account)) {
                r.err = error::bad_field_value;
                return r;
            }
            break;
        case 11:  // ClOrdID
            if (!to_uint(bf.value, clordid)) {
                r.err = error::bad_field_value;
                return r;
            }
            has_clordid = true;
            break;
        case 41:  // OrigClOrdID
            if (!to_uint(bf.value, orig_id)) {
                r.err = error::bad_field_value;
                return r;
            }
            has_orig = true;
            break;
        case 38:  // OrderQty
            if (!to_uint(bf.value, qty)) {
                r.err = error::bad_field_value;
                return r;
            }
            has_qty = true;
            break;
        case 44:  // Price (in ticks)
            if (!to_uint(bf.value, px)) {
                r.err = error::bad_field_value;
                return r;
            }
            has_px = true;
            break;
        case 54:  // Side (1=Buy, 2=Sell)
            if (bf.value == "1")
                sd = side::bid;
            else if (bf.value == "2")
                sd = side::ask;
            else {
                r.err = error::bad_field_value;
                return r;
            }
            has_side = true;
            break;
        case 59:  // TimeInForce (0=Day, 1=GTC, 3=IOC, 4=FOK)
            if (bf.value == "0" || bf.value == "1")
                tf = tif::gtc;
            else if (bf.value == "3")
                tf = tif::ioc;
            else if (bf.value == "4")
                tf = tif::fok;
            else {
                r.err = error::bad_field_value;
                return r;
            }
            break;
        default:
            break;  // Symbol(55), TransactTime(60), OrdType(40), ... not needed here.
        }
    }

    if (msg_type == "D") {
        if (!has_clordid || !has_side || !has_qty || !has_px) {
            r.err = error::missing_field;
            return r;
        }
        r.cmd = command::make_submit(submit_msg{
            .id = clordid,
            .px = px,
            .qty = qty,
            .s = sd,
            .t = tf,
            ._pad = 0,
            .account_id = account,
        });
    } else if (msg_type == "F") {
        if (!has_orig) {
            r.err = error::missing_field;
            return r;
        }
        r.cmd = command::make_cancel(cancel_msg{.id = orig_id});
    } else if (msg_type == "G") {
        if (!has_orig || !has_qty || !has_px) {
            r.err = error::missing_field;
            return r;
        }
        r.cmd = command::make_modify(modify_msg{.id = orig_id, .new_px = px, .new_qty = qty});
    } else {
        r.err = error::unsupported_msg_type;
        return r;
    }

    r.err = error::ok;
    r.consumed = total;
    return r;
}

}  // namespace lob::fix

#endif  // LOB_FIX_PARSER_HPP
