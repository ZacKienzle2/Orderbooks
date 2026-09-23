#ifndef LOB_JSON_RECORDER_HPP
#define LOB_JSON_RECORDER_HPP

#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <ios>
#include <limits>
#include <ostream>
#include <string_view>

namespace lob {

// Publisher that writes one JSON Lines event per emit to an output stream.
//
// Format: each line is a self-describing JSON object with a "kind" tag
// (fill / top / trade / self_trade / reject), the engine seq, and the
// type-specific fields. Numeric fields are emitted as JSON numbers;
// std::uint64_t values fit in IEEE 754 binary64 up to 2^53 - 1, which covers
// any realistic order_id / seq / qty for testing and visualisation.
//
// Intended for the replay tool and for analysis pipelines that consume the
// event stream from a Python harness; not used on the production hot path.
//
// Encoding: each publish() names its fields once and line_ formats them into
// a fixed stack buffer via std::to_chars (locale-free, allocation-free), then
// issues a single out_.write() call. This avoids the per-call locale lock that
// an operator<< chain pays on every numeric field.
//
// noexcept: publish() is declared noexcept to satisfy the publisher
// concept. The caller is responsible for constructing out_ with the
// default exceptions() mask (goodbit); a stream configured to throw on
// badbit/failbit would call std::terminate from inside publish().
class json_recorder {
   public:
    explicit json_recorder(std::ostream& out) noexcept : out_(out) {
        // publish() is declared noexcept to satisfy the publisher concept,
        // but out_.write can throw if the stream has any exception bits
        // enabled. A throw from inside noexcept calls std::terminate.
        // Lock the contract at construction time so the failure is visible
        // here rather than as a process abort on the first publish.
        assert(out.exceptions() == std::ios::goodbit &&
               "json_recorder: stream must not have exceptions enabled");
    }

    void publish(const fill_msg& m) noexcept {
        line_("fill", m.seq,
              {{"maker", m.maker}, {"taker", m.taker}, {"px", m.px}, {"qty", m.qty}});
    }

    void publish(const top_msg& m) noexcept {
        line_("top", m.seq,
              {{"bid_px", m.bid_px},
               {"ask_px", m.ask_px},
               {"bid_qty", m.bid_qty},
               {"ask_qty", m.ask_qty}});
    }

    void publish(const trade_msg& m) noexcept {
        line_("trade", m.seq, {{"px", m.px}, {"qty", m.qty}});
    }

    void publish(const self_trade_msg& m) noexcept {
        line_("self_trade", m.seq,
              {{"aggressor", m.aggressor},
               {"resting", m.resting},
               {"account", m.account},
               {"px", m.px},
               {"qty", m.qty}});
    }

    void publish(const reject_msg& m) noexcept {
        line_("reject", m.seq,
              {{"id", m.id},
               {"account", m.account},
               {"px", m.px},
               {"qty", m.qty},
               {"reason", static_cast<std::uint64_t>(m.reason)}});
    }

   private:
    struct field {
        std::string_view key;
        std::uint64_t value;
    };

    // The longest line, a self_trade, is under 200 bytes: five keys of at
    // most twelve bytes, each value at most twenty digits, and the fixed
    // punctuation. The buffer leaves room for a field more.
    static constexpr std::size_t line_capacity = 384;
    static constexpr std::size_t max_fields = 6;

    void line_(std::string_view kind, seq_t seq, std::initializer_list<field> fields) noexcept {
        assert(fields.size() <= max_fields && "json_recorder: line_capacity sized for six fields");
        std::array<char, line_capacity> buf{};
        char* p = buf.data();
        p = append_(p, R"({"kind":")");
        p = append_(p, kind);
        p = append_(p, R"(","seq":)");
        p = append_num_(p, seq);
        for (const field& f : fields) {
            p = append_(p, R"(,")");
            p = append_(p, f.key);
            p = append_(p, R"(":)");
            p = append_num_(p, f.value);
        }
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

    static char* append_(char* p, std::string_view s) noexcept {
        std::memcpy(p, s.data(), s.size());
        return p + s.size();
    }

    // Format an integer into a local temporary just wide enough for the type,
    // read off the type rather than counted by hand: digits10 is the largest
    // power of ten that fits, so one more digit covers the rest of the range.
    // The bounded local write lets GCC's -Werror=array-bounds analysis prove
    // that subsequent appends into the caller's buffer remain in range;
    // without it, GCC tracks the worst case of to_chars writing all the way to
    // end and concludes the trailing literal could overflow.
    static constexpr std::size_t max_digits = std::numeric_limits<std::uint64_t>::digits10 + 1;

    static char* append_num_(char* p, std::uint64_t v) noexcept {
        std::array<char, max_digits> digits{};
        const auto r = std::to_chars(digits.data(), digits.data() + digits.size(), v);
        const auto n = static_cast<std::size_t>(r.ptr - digits.data());
        std::memcpy(p, digits.data(), n);
        return p + n;
    }

    std::ostream& out_;
};

}  // namespace lob

#endif  // LOB_JSON_RECORDER_HPP
