#ifndef LOB_JSON_RECORDER_HPP
#define LOB_JSON_RECORDER_HPP

#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
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
    explicit json_recorder(std::ostream& out) noexcept : out_(out) {}

    void publish(const fill_msg& m) noexcept {
        std::array<char, 384> buf{};
        char* p = buf.data();
        constexpr std::string_view head{R"({"kind":"fill","seq":)"};
        p = append_(p, head);
        p = append_num_(p, buf.data() + buf.size(), m.seq);
        p = append_(p, R"(,"maker":)");
        p = append_num_(p, buf.data() + buf.size(), m.maker);
        p = append_(p, R"(,"taker":)");
        p = append_num_(p, buf.data() + buf.size(), m.taker);
        p = append_(p, R"(,"px":)");
        p = append_num_(p, buf.data() + buf.size(), m.px);
        p = append_(p, R"(,"qty":)");
        p = append_num_(p, buf.data() + buf.size(), m.qty);
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

    void publish(const top_msg& m) noexcept {
        std::array<char, 384> buf{};
        char* p = buf.data();
        constexpr std::string_view head{R"({"kind":"top","seq":)"};
        p = append_(p, head);
        p = append_num_(p, buf.data() + buf.size(), m.seq);
        p = append_(p, R"(,"bid_px":)");
        p = append_num_(p, buf.data() + buf.size(), m.bid_px);
        p = append_(p, R"(,"ask_px":)");
        p = append_num_(p, buf.data() + buf.size(), m.ask_px);
        p = append_(p, R"(,"bid_qty":)");
        p = append_num_(p, buf.data() + buf.size(), m.bid_qty);
        p = append_(p, R"(,"ask_qty":)");
        p = append_num_(p, buf.data() + buf.size(), m.ask_qty);
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

    void publish(const trade_msg& m) noexcept {
        std::array<char, 384> buf{};
        char* p = buf.data();
        constexpr std::string_view head{R"({"kind":"trade","seq":)"};
        p = append_(p, head);
        p = append_num_(p, buf.data() + buf.size(), m.seq);
        p = append_(p, R"(,"px":)");
        p = append_num_(p, buf.data() + buf.size(), m.px);
        p = append_(p, R"(,"qty":)");
        p = append_num_(p, buf.data() + buf.size(), m.qty);
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

    void publish(const self_trade_msg& m) noexcept {
        std::array<char, 384> buf{};
        char* p = buf.data();
        constexpr std::string_view head{R"({"kind":"self_trade","seq":)"};
        p = append_(p, head);
        p = append_num_(p, buf.data() + buf.size(), m.seq);
        p = append_(p, R"(,"aggressor":)");
        p = append_num_(p, buf.data() + buf.size(), m.aggressor);
        p = append_(p, R"(,"resting":)");
        p = append_num_(p, buf.data() + buf.size(), m.resting);
        p = append_(p, R"(,"account":)");
        p = append_num_(p, buf.data() + buf.size(), m.account);
        p = append_(p, R"(,"px":)");
        p = append_num_(p, buf.data() + buf.size(), m.px);
        p = append_(p, R"(,"qty":)");
        p = append_num_(p, buf.data() + buf.size(), m.qty);
        p = append_(p, "}\n");
        out_.write(buf.data(), p - buf.data());
    }

  private:
    static char* append_(char* p, std::string_view s) noexcept {
        std::memcpy(p, s.data(), s.size());
        return p + s.size();
    }

    template <class T>
    static char* append_num_(char* p, char* end, T v) noexcept {
        const auto r = std::to_chars(p, end, v);
        return r.ec == std::errc{} ? r.ptr : p;
    }

    std::ostream& out_;
};

}  // namespace lob

#endif  // LOB_JSON_RECORDER_HPP
