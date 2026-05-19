#ifndef LOB_JSON_RECORDER_HPP
#define LOB_JSON_RECORDER_HPP

#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <cstdint>
#include <ostream>
#include <string>

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
class json_recorder {
public:
    explicit json_recorder(std::ostream& out) noexcept : out_(out) {}

    void publish(fill_msg const& m) noexcept {
        out_ << R"({"kind":"fill","seq":)" << m.seq
             << R"(,"maker":)" << m.maker
             << R"(,"taker":)" << m.taker
             << R"(,"px":)"    << m.px
             << R"(,"qty":)"   << m.qty
             << "}\n";
    }

    void publish(top_msg const& m) noexcept {
        out_ << R"({"kind":"top","seq":)" << m.seq
             << R"(,"bid_px":)"  << m.bid_px
             << R"(,"ask_px":)"  << m.ask_px
             << R"(,"bid_qty":)" << m.bid_qty
             << R"(,"ask_qty":)" << m.ask_qty
             << "}\n";
    }

    void publish(trade_msg const& m) noexcept {
        out_ << R"({"kind":"trade","seq":)" << m.seq
             << R"(,"px":)"  << m.px
             << R"(,"qty":)" << m.qty
             << "}\n";
    }

    void publish(self_trade_msg const& m) noexcept {
        out_ << R"({"kind":"self_trade","seq":)" << m.seq
             << R"(,"aggressor":)" << m.aggressor
             << R"(,"resting":)"   << m.resting
             << R"(,"account":)"   << m.account
             << R"(,"px":)"        << m.px
             << R"(,"qty":)"       << m.qty
             << "}\n";
    }

private:
    std::ostream& out_;
};

}  // namespace lob

#endif  // LOB_JSON_RECORDER_HPP
