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
#include <ios>
#include <ostream>
#include <string_view>
#include <system_error>

namespace lob {

// Publisher that writes one JSON Lines event per emit to an output stream.
//
// Format: each line is a self-describing JSON object with a "kind" tag
// (fill / top / trade / self_trade), the engine seq, and the type-specific
// fields. Numeric fields are emitted as JSON numbers; std::uint64_t values
// fit in IEEE 754 binary64 up to 2^53 - 1, which covers any realistic
// order_id / seq / qty for testing and visualisation.
//
// Intended for the replay tool and for analysis pipelines that consume the
// event stream from a Python harness; not used on the production hot path.
//
// Encoding: each publish() formats the line into a fixed stack buffer via
// std::to_chars (locale-free, allocation-free) and issues a single
// out_.write() call. This avoids the per-call locale lock that the
// previous operator<< chain paid on every numeric field.
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
        std::array<char, 384> buf{};
        char* p = buf.data();
        constexpr std::string_view head{R"({"kind":"fill","seq":)"};
        p = append_(p, head);
        p = append_num_(p, m.seq);
        p = append_(p, R"(,"maker":)");
        p = append_num_(p, m.maker);
        p = append_(p, R"(,"taker":)");
        p = append_num_(p, m.taker);
        p = append_(p, R"(,"px":)");
        p = append_num_(p, m.px);
        p = append_(p, R"(,"qty":)");
        p = append_num_(p, m.qty);
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

    void publish(const top_msg& m) noexcept {
        std::array<char, 384> buf{};
        char* p = buf.data();
        constexpr std::string_view head{R"({"kind":"top","seq":)"};
        p = append_(p, head);
        p = append_num_(p, m.seq);
        p = append_(p, R"(,"bid_px":)");
        p = append_num_(p, m.bid_px);
        p = append_(p, R"(,"ask_px":)");
        p = append_num_(p, m.ask_px);
        p = append_(p, R"(,"bid_qty":)");
        p = append_num_(p, m.bid_qty);
        p = append_(p, R"(,"ask_qty":)");
        p = append_num_(p, m.ask_qty);
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

    void publish(const trade_msg& m) noexcept {
        std::array<char, 384> buf{};
        char* p = buf.data();
        constexpr std::string_view head{R"({"kind":"trade","seq":)"};
        p = append_(p, head);
        p = append_num_(p, m.seq);
        p = append_(p, R"(,"px":)");
        p = append_num_(p, m.px);
        p = append_(p, R"(,"qty":)");
        p = append_num_(p, m.qty);
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

    void publish(const self_trade_msg& m) noexcept {
        std::array<char, 384> buf{};
        char* p = buf.data();
        constexpr std::string_view head{R"({"kind":"self_trade","seq":)"};
        p = append_(p, head);
        p = append_num_(p, m.seq);
        p = append_(p, R"(,"aggressor":)");
        p = append_num_(p, m.aggressor);
        p = append_(p, R"(,"resting":)");
        p = append_num_(p, m.resting);
        p = append_(p, R"(,"account":)");
        p = append_num_(p, m.account);
        p = append_(p, R"(,"px":)");
        p = append_num_(p, m.px);
        p = append_(p, R"(,"qty":)");
        p = append_num_(p, m.qty);
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

  private:
    static char* append_(char* p, std::string_view s) noexcept {
        std::memcpy(p, s.data(), s.size());
        return p + s.size();
    }

    // Format an integer into a tight 24-byte local temporary (max digit
    // count for an unsigned 64-bit value is 20, so 24 covers any signed
    // 64-bit value with sign and slack). The bounded local write lets
    // GCC's -Werror=array-bounds analysis prove that subsequent appends
    // into the caller's buffer remain in range; without it, GCC tracks
    // the worst case of to_chars writing all the way to end and concludes
    // the trailing literal could overflow.
    template <class T>
    static char* append_num_(char* p, T v) noexcept {
        std::array<char, 24> tmp{};
        const auto r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), v);
        const auto n = static_cast<std::size_t>(r.ptr - tmp.data());
        std::memcpy(p, tmp.data(), n);
        return p + n;
    }

    std::ostream& out_;
};

}  // namespace lob

#endif  // LOB_JSON_RECORDER_HPP
